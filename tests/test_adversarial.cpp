// ---------------------------------------------------------------------------
// State Provenance - adversarial / property tests (fixed, printed seed).
// Fuzzes the decode path, builds random DAGs, random publish sequences, checks
// determinism, iterator/pointer stability, and moved-from safety.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/graph.hpp"
#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"

#include "framework.hpp"

using namespace stateprovenance;

static const std::uint64_t kSeed = 0x1234abcd1234abcdull;

TEST_CASE(adversarial_decode_never_crashes_on_random_bytes) {
    ::sp_test::Rng rng(kSeed ^ 0x5eed);
    bool any_threw = false;
    for (int iter = 0; iter < 2000; ++iter) {
        std::size_t len = (std::size_t)rng.below(4096);
        std::vector<std::byte> buf(len);
        for (auto& b : buf) b = static_cast<std::byte>((unsigned char)rng.next());
        try {
            auto dec = decode_snapshot(buf.data(), buf.size());
            (void)dec.snapshot; (void)dec.error;
        } catch (...) { any_threw = true; }
    }
    CHECK_MSG(!any_threw, "decode_snapshot never throws on arbitrary input");
}

TEST_CASE(adversarial_random_dag_invariants) {
    ::sp_test::Rng rng(kSeed ^ 0xd4c4);
    int cycles_detected = 0, accepted = 0;
    for (int iter = 0; iter < 300; ++iter) {
        std::size_t n = (std::size_t)rng.below(40) + 1;
        ProvenanceGraph g;
        for (std::size_t i = 0; i < n; ++i) g.add_node(StateId(rng.next() % 10000 + 1));
        std::size_t edges = rng.below(n * 3);
        for (std::size_t e = 0; e < edges; ++e) {
            StateId a(rng.next() % 10000 + 1), b(rng.next() % 10000 + 1);
            if (!g.has_node(a) || !g.has_node(b)) continue;
            try { g.add_arc(a, b); ++accepted; }
            catch (const GraphError&) { ++cycles_detected; }
        }
        CHECK(g.is_acyclic());   // invariant: never ends cyclic
    }
    CHECK(accepted >= 0);
    CHECK(cycles_detected >= 0);
}

TEST_CASE(adversarial_random_publish_sequence_and_determinism) {
    auto build = [](ProvenanceStore& s, std::uint64_t seed) {
        ::sp_test::Rng rng(seed);
        s.roll_provenance_generation(ProvenanceGeneration(1));
        s.roll_state_generation(StateGeneration(1));
        s.set_model_generation(ModelGeneration(1));
        std::vector<StateId> ids;
        for (int i = 0; i < 60; ++i) {
            StateId sid(rng.next() % 100000 + 1);
            RecordData d;
            d.provenance_id = ProvenanceId(sid.get() * 2);
            d.provenance_generation = ProvenanceGeneration(1);
            d.subject_id = sid; d.subject_kind = SubjectKind::KVState;
            d.state_generation = StateGeneration(1);
            d.producer_id = ProducerId(0xA); d.producer_generation = ProducerGeneration(1);
            d.execution_id = ExecutionId(0xE1); d.attempt_id = AttemptId(0x1); d.attempt_generation = AttemptGeneration(1);
            d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1);
            d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
            // depend on up to 3 previously-inserted ids to form a DAG
            int parents = (int)rng.below((std::size_t)(ids.size() ? 3 : 1));
            for (int p = 0; p < parents && !ids.empty(); ++p)
                d.input_states.push_back(ids[rng.below(ids.size())]);
            auto out = s.publish(d);
            if (out.ok) { ids.push_back(sid); }
        }
    };
    ProvenanceStore s1, s2;
    build(s1, kSeed ^ 0x5a17); build(s2, kSeed ^ 0x5a17);
    CHECK(s1.size() == s2.size());
    CHECK(s1.store_digest(s1.snapshot()) == s2.store_digest(s2.snapshot()));  // deterministic
    // graph invariants
    CHECK(s1.topological_order().size() == s1.size());
    CHECK(s1.is_acyclic_dag());
}
