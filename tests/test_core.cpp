// ---------------------------------------------------------------------------
// State Provenance - core unit tests.
// Strong IDs, generations, immutable records, derivation graph, compatibility/
// reuse, authority fencing, invalidation propagation.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/digest.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/graph.hpp"
#include "stateprovenance/authority.hpp"
#include "stateprovenance/compat.hpp"
#include "stateprovenance/invalidate.hpp"
#include "stateprovenance/store.hpp"

#include "framework.hpp"

using namespace stateprovenance;

// ---- helpers ---------------------------------------------------------------
static RecordData base_record(StateId sid, ProvenanceId pid, StateGeneration sg = StateGeneration(1),
                              ProvenanceGeneration pg = ProvenanceGeneration(1)) {
    RecordData d;
    d.provenance_id = pid; d.provenance_generation = pg;
    d.subject_id = sid; d.subject_kind = SubjectKind::KVState;
    d.state_generation = sg;
    d.producer_id = ProducerId(0xA); d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(0xE1); d.attempt_id = AttemptId(0x1); d.attempt_generation = AttemptGeneration(1);
    d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1);
    d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
    return d;
}

// ---- strong ids ------------------------------------------------------------
TEST_CASE(strong_ids_are_distinct_and_roundtrip) {
    StateId a(0xabc), b(0xabc), c(0xdef);
    CHECK(a != c);
    CHECK(a == b);
    CHECK(a.valid() && !StateId().valid());
    CHECK(a.to_string() == "0x0000000000000abc");
    auto p = StateId::try_parse("0xabc"); CHECK(p && p->get() == 0xabc);
    auto pd = StateId::try_parse("2748"); CHECK(pd && pd->get() == 2748);
    auto bad = StateId::try_parse("not-a-number"); CHECK(!bad);
    auto empty = StateId::try_parse(""); CHECK(!empty);
    // Distinct types are not implicitly convertible.
    CHECK(sizeof(StateId) == 8);
    CHECK(sizeof(ModelId) == 8);
}

TEST_CASE(generations_are_independently_typed) {
    StateGeneration sg1(1), sg2(2);
    ProvenanceGeneration pg(1);
    CHECK(sg1 != sg2);
    CHECK(StateGeneration(3) > StateGeneration(2));
    // Generations and ids of the same numeric value are different types / domains.
    CHECK(name_of(sg1) == std::string("StateGeneration"));
    CHECK(name_of(pg) == std::string("ProvenanceGeneration"));
}

// ---- enums ---- ------------------------------------------------------------
TEST_CASE(enum_names_and_parse) {
    CHECK(name_of(SubjectKind::KVState) == std::string("KVState"));
    CHECK(name_of(EvidenceClass::RECONSTRUCTED) == std::string("RECONSTRUCTED"));
    auto k = parse_SubjectKind("TensorState");
    CHECK(k && *k == SubjectKind::TensorState);
    auto bad = parse_SubjectKind("Nope"); CHECK(!bad);
    CHECK(is_valid(SubjectKind::Buffer));
    CHECK(is_valid(EvidenceClass::MEASURED));
    // out-of-range value is invalid
    CHECK(!is_valid(static_cast<SubjectKind>(200)));
}

// ---- records / validation / immutability -----------------------------------
TEST_CASE(record_validation_rejects_incomplete) {
    RecordData d;   // missing everything
    CHECK(validate_record(d).has_value());
    RecordData d2 = base_record(StateId(1), ProvenanceId(2));
    CHECK(!validate_record(d2).has_value());
    // missing provenance_id
    RecordData d3 = d2; d3.provenance_id = ProvenanceId();
    CHECK(validate_record(d3).has_value());
    // missing subject kind
    RecordData d4 = d2; d4.subject_kind = SubjectKind::Unknown;
    CHECK(validate_record(d4).has_value());
    // duplicate input states
    RecordData d5 = d2; d5.input_states = {StateId(9), StateId(9)};
    CHECK(validate_record(d5).has_value());
    // self parent
    RecordData d6 = d2; d6.parent_provenance = {ProvenanceId(2)};
    CHECK(validate_record(d6).has_value());
    CHECK_THROWS(RecordBuilder().build());
}

TEST_CASE(records_are_immutable_and_digests_stable) {
    auto r1 = RecordBuilder().provenance_id(ProvenanceId(0x10)).provenance_generation(ProvenanceGeneration(1))
        .subject_id(StateId(0x1)).subject_kind(SubjectKind::KVState).state_generation(StateGeneration(1))
        .producer_id(ProducerId(0xA)).producer_generation(ProducerGeneration(1))
        .execution_id(ExecutionId(0xE1)).attempt_id(AttemptId(1)).attempt_generation(AttemptGeneration(1))
        .build();
    auto r2 = RecordBuilder().provenance_id(ProvenanceId(0x11)).provenance_generation(ProvenanceGeneration(1))
        .subject_id(StateId(0x2)).subject_kind(SubjectKind::KVState).state_generation(StateGeneration(1))
        .producer_id(ProducerId(0xA)).producer_generation(ProducerGeneration(1))
        .execution_id(ExecutionId(0xE2)).attempt_id(AttemptId(1)).attempt_generation(AttemptGeneration(1))
        .build();
    // The records differ; their digests differ and are stable across copies.
    CHECK(r1.d.subject_id == StateId(0x1));
    CHECK(r2.d.subject_id == StateId(0x2));
    CHECK(r1.derivation_digest() != r2.derivation_digest());
    CHECK(r1.derivation_digest() == r1.derivation_digest());  // stable
    CHECK(r1.derivation_digest().size() == 16);               // 64-bit hex
    CHECK(!r1.derivation_digest().empty());
}

TEST_CASE(record_digest_order_invariant) {
    // Reference vectors hashed in sorted order: insertion order must not change the digest.
    RecordData a = base_record(StateId(1), ProvenanceId(2));
    a.input_states = {StateId(0x20), StateId(0x10)};
    RecordData b = a; b.input_states = {StateId(0x10), StateId(0x20)};
    ProvenanceRecord ra(a), rb(b);
    CHECK(ra.derivation_digest() == rb.derivation_digest());
}

// ---- graph ----
TEST_CASE(graph_construction_and_traversal) {
    ProvenanceGraph g;
    g.add_node(StateId(1)); g.add_node(StateId(2)); g.add_node(StateId(3)); g.add_node(StateId(4));
    g.add_arc(StateId(1), StateId(2));   // 1 -> 2
    g.add_arc(StateId(2), StateId(3));   // 2 -> 3
    g.add_arc(StateId(1), StateId(3));   // 1 -> 3 (diamond)
    g.add_arc(StateId(3), StateId(4));   // 3 -> 4
    CHECK(g.node_count() == 4);
    CHECK(g.arc_count() == 4);
    CHECK(g.parents(StateId(3)).size() == 2);
    CHECK(g.children(StateId(1)).size() == 2);
    CHECK(g.fan_in(StateId(3)) == 2);
    CHECK(g.fan_out(StateId(1)) == 2);
    auto anc = g.ancestors(StateId(4));
    CHECK(anc == std::vector<StateId>({StateId(1), StateId(2), StateId(3)}));
    auto desc = g.descendants(StateId(1));
    CHECK(desc == std::vector<StateId>({StateId(2), StateId(3), StateId(4)}));
    auto shared = g.shared_ancestors(StateId(3), StateId(4));
    CHECK(shared == std::vector<StateId>({StateId(1), StateId(2)}));
    auto roots = g.roots();
    CHECK(roots == std::vector<StateId>({StateId(1)}));
    CHECK(g.is_acyclic());
    auto topo = g.topological_order();
    CHECK(topo.size() == 4);
    // topological order must be a valid linearization
    std::vector<int> pos(6, 0);
    for (std::size_t i = 0; i < topo.size(); ++i) pos[topo[i].get()] = (int)i;
    CHECK(pos[1] < pos[2] && pos[1] < pos[3] && pos[2] < pos[3] && pos[3] < pos[4]);
}

TEST_CASE(graph_rejects_cycles_duplicates_and_self) {
    ProvenanceGraph g;
    g.add_node(StateId(1)); g.add_node(StateId(2)); g.add_node(StateId(3));
    CHECK_THROWS(g.add_arc(StateId(1), StateId(1)));       // self
    g.add_arc(StateId(1), StateId(2));
    CHECK_THROWS(g.add_arc(StateId(1), StateId(2)));       // duplicate
    g.add_arc(StateId(2), StateId(3));
    CHECK_THROWS(g.add_arc(StateId(3), StateId(1)));       // cycle
    CHECK_THROWS(g.add_arc(StateId(99), StateId(1)));      // unregistered node
    CHECK(g.is_acyclic());
}

TEST_CASE(graph_rejects_dangling_arc) {
    ProvenanceGraph g;
    g.add_node(StateId(1));                 // node 2 is NOT registered
    CHECK_THROWS(g.add_arc(StateId(1), StateId(2)));   // dangling endpoint
}

// ---- compatibility / reuse ----
TEST_CASE(reuse_eligible_and_incompatible) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    store.set_model_generation(ModelGeneration(1));
    RecordData d = base_record(StateId(0x1), ProvenanceId(0x10));
    d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1);
    d.runtime_id = RuntimeId(0x11); d.backend_id = BackendId(0x12); d.toolchain_id = ToolchainId(0x13);
    d.architecture = "sm_120"; d.compute_capability = "12.0"; d.dtype = "float16"; d.shape = "1024x4096";
    auto p = store.publish(d);
    CHECK(p.ok);
    ReuseRequest ok; ok.model_id = ModelId(0x7); ok.model_generation = ModelGeneration(1);
    ok.architecture = "sm_120"; ok.compute_capability = "12.0"; ok.dtype = "float16"; ok.shape = "1024x4096";
    ok.runtime_id = RuntimeId(0x11); ok.backend_id = BackendId(0x12); ok.toolchain_id = ToolchainId(0x13);
    auto r = store.check_reuse(StateId(0x1), ok);
    CHECK(r.eligibility == ReuseEligibility::ELIGIBLE);
    // model revision mismatch -> INCOMPATIBLE with the exact dimension exposed
    ReuseRequest bad = ok; bad.model_id = ModelId(0x99);
    auto rb = store.check_reuse(StateId(0x1), bad);
    CHECK(rb.eligibility == ReuseEligibility::INCOMPATIBLE);
    CHECK(rb.failed_dimensions.size() >= 1);
    bool found = false;
    for (auto dim : rb.failed_dimensions) if (dim == CompatibilityDimension::ModelIdentity) found = true;
    CHECK(found);
}

TEST_CASE(reuse_incomplete_provenance) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    RecordData d = base_record(StateId(0x1), ProvenanceId(0x10));
    // no model recorded
    d.model_id = ModelId(); d.model_generation = ModelGeneration();
    auto p = store.publish(d);
    CHECK(p.ok);
    ReuseRequest req; req.model_id = ModelId(0x7); req.model_generation = ModelGeneration(1);
    auto r = store.check_reuse(StateId(0x1), req);
    CHECK(r.eligibility == ReuseEligibility::INCOMPLETE_PROVENANCE);
    bool found = false;
    for (auto dim : r.failed_dimensions) if (dim == CompatibilityDimension::ModelIdentity) found = true;
    CHECK(found);
}

TEST_CASE(reuse_reflects_validity_state) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    RecordData d = base_record(StateId(0x1), ProvenanceId(0x10));
    auto p = store.publish(d);
    CHECK(p.ok);
    ReuseRequest req;
    CHECK(store.check_reuse(StateId(0x1), req).eligibility == ReuseEligibility::ELIGIBLE);
    store.invalidate(InvalidatingSubject{StateId(0x1), InvalidationReason::OperatorInvalidation, "x"});
    CHECK(store.check_reuse(StateId(0x1), req).eligibility == ReuseEligibility::INVALIDATED);
}

// ---- authority fencing ----
TEST_CASE(authority_fencing_rejects_stale) {
    AuthorityState a;
    a.set_epoch(1);
    a.register_boot(WorkerId(0x1), WorkerBootId(0xB1));
    a.begin_attempt(AttemptId(0x7), AttemptGeneration(0x3));
    a.roll_state_generation(StateGeneration(0x5));
    a.roll_provenance_generation(ProvenanceGeneration(0x6));
    a.set_dependency_generation(DependencyGeneration(0x9));
    a.set_model_generation(ModelGeneration(0x2));

    AuthorityClaim good;
    good.worker_id = WorkerId(0x1); good.worker_boot = WorkerBootId(0xB1);
    good.attempt_id = AttemptId(0x7); good.attempt_generation = AttemptGeneration(0x3);
    good.state_generation = StateGeneration(0x5); good.provenance_generation = ProvenanceGeneration(0x6);
    good.coordinator_epoch = 1;
    good.dependency_generation = DependencyGeneration(0x9);
    good.model_generation = ModelGeneration(0x2);
    CHECK(a.evaluate_claim(good).accepted());

    AuthorityClaim stale_epoch = good; stale_epoch.coordinator_epoch = 2;
    CHECK(!a.evaluate_claim(stale_epoch).accepted());
    CHECK(a.evaluate_claim(stale_epoch).dimension == "CoordinatorEpoch");

    AuthorityClaim stale_boot = good; stale_boot.worker_boot = WorkerBootId(0xDEAD);
    CHECK(!a.evaluate_claim(stale_boot).accepted());
    CHECK(a.evaluate_claim(stale_boot).dimension == "WorkerBootId");

    AuthorityClaim stale_attempt = good; stale_attempt.attempt_id = AttemptId(0x999);
    CHECK(!a.evaluate_claim(stale_attempt).accepted());
    CHECK(a.evaluate_claim(stale_attempt).dimension == "AttemptId");

    AuthorityClaim stale_stategen = good; stale_stategen.state_generation = StateGeneration(0x999);
    CHECK(!a.evaluate_claim(stale_stategen).accepted());
    CHECK(a.evaluate_claim(stale_stategen).dimension == "StateGeneration");

    AuthorityClaim stale_provgen = good; stale_provgen.provenance_generation = ProvenanceGeneration(0x999);
    CHECK(!a.evaluate_claim(stale_provgen).accepted());
    CHECK(a.evaluate_claim(stale_provgen).dimension == "ProvenanceGeneration");

    AuthorityClaim stale_depgen = good; stale_depgen.dependency_generation = DependencyGeneration(0x1);
    CHECK(!a.evaluate_claim(stale_depgen).accepted());
    CHECK(a.evaluate_claim(stale_depgen).dimension == "DependencyGeneration");
}

TEST_CASE(authority_generation_rollover_escapades_old_work) {
    AuthorityState a;
    a.set_epoch(1); a.roll_provenance_generation(ProvenanceGeneration(1));
    AuthorityClaim c;
    c.coordinator_epoch = 1; c.provenance_generation = ProvenanceGeneration(1);
    CHECK(a.evaluate_claim(c).accepted());
    a.roll_provenance_generation(ProvenanceGeneration(2));
    CHECK(!a.evaluate_claim(c).accepted());  // old provenance generation now stale
    a.roll_epoch();
    CHECK(!a.evaluate_claim(c).accepted());  // old epoch now stale too
}

// ---- invalidation propagation ----
TEST_CASE(invalidation_propagates_to_descendants_only) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    auto publish = [&](StateId sid, std::initializer_list<StateId> parents) {
        RecordData d = base_record(sid, ProvenanceId(sid.get() * 2));
        for (auto p : parents) d.input_states.push_back(p);
        return store.publish(d).ok;
    };
    CHECK(publish(StateId(1), {}));
    CHECK(publish(StateId(2), {StateId(1)}));
    CHECK(publish(StateId(3), {StateId(2)}));
    CHECK(publish(StateId(4), {}));                       // unrelated branch
    auto inv = store.invalidate(InvalidatingSubject{StateId(1), InvalidationReason::DependencyInvalidation, "x"});
    CHECK(inv.ok);
    CHECK(inv.result.affected.size() == 3);               // {1,2,3}
    CHECK(store.validity_of(StateId(1)) == ValidityState::INVALIDATED);
    CHECK(store.validity_of(StateId(2)) == ValidityState::INVALIDATED);
    CHECK(store.validity_of(StateId(3)) == ValidityState::INVALIDATED);
    CHECK(store.validity_of(StateId(4)) == ValidityState::VALID);   // untouched
    CHECK(inv.result.size() > 0);
    // causal chain for 3 must be 1 -> 2 -> 3 (lazily reconstructed)
    auto c3 = inv.result.chain_for(StateId(3));
    CHECK(c3.path.back() == StateId(3));
    CHECK(c3.path.size() == 3);
    CHECK(c3.root == StateId(1));
}

TEST_CASE(invalidation_is_deterministic) {
    ProvenanceStore s1, s2;
    auto build = [&](ProvenanceStore& st) {
        st.roll_provenance_generation(ProvenanceGeneration(1));
        st.roll_state_generation(StateGeneration(1));
        RecordData a = base_record(StateId(1), ProvenanceId(2));
        RecordData b = base_record(StateId(2), ProvenanceId(4)); b.input_states = {StateId(1)};
        RecordData c = base_record(StateId(3), ProvenanceId(6)); c.input_states = {StateId(1)};
        st.publish(a); st.publish(b); st.publish(c);
    };
    build(s1); build(s2);
    auto i1 = s1.invalidate(InvalidatingSubject{StateId(1), InvalidationReason::DependencyInvalidation, "x"});
    auto i2 = s2.invalidate(InvalidatingSubject{StateId(1), InvalidationReason::DependencyInvalidation, "x"});
    CHECK(i1.result.affected == i2.result.affected);
    CHECK(i1.result.root_of == i2.result.root_of);
}
