#include <cstdio>
#include <memory>
#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/digest.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/graph.hpp"
#include "stateprovenance/authority.hpp"
#include "stateprovenance/compat.hpp"
#include "stateprovenance/invalidate.hpp"
#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"
#include "stateprovenance/explain.hpp"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while(0)

int main() {
    using namespace stateprovenance;

    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    store.set_model_generation(ModelGeneration(1));
    store.set_adapter_generation(AdapterGeneration(1));
    store.set_policy_generation(PolicyGeneration(1));
    store.begin_attempt(AttemptId(0x1), AttemptGeneration(1));

    RecordData root;
    root.provenance_id = ProvenanceId(0x10);
    root.provenance_generation = ProvenanceGeneration(1);
    root.subject_id = StateId(0x1);
    root.subject_kind = SubjectKind::KVState;
    root.state_generation = StateGeneration(1);
    root.producer_id = ProducerId(0xA);
    root.producer_generation = ProducerGeneration(1);
    root.execution_id = ExecutionId(0xE1);
    root.attempt_id = AttemptId(0x1);
    root.attempt_generation = AttemptGeneration(1);
    root.model_id = ModelId(0x7);
    root.model_generation = ModelGeneration(1);

    auto p0 = store.publish(root);
    CHECK(p0.ok);

    // child derived from root via input_state
    RecordData child;
    child.provenance_id = ProvenanceId(0x11);
    child.provenance_generation = ProvenanceGeneration(1);
    child.subject_id = StateId(0x2);
    child.subject_kind = SubjectKind::TensorState;
    child.state_generation = StateGeneration(1);
    child.producer_id = ProducerId(0xA);
    child.producer_generation = ProducerGeneration(1);
    child.execution_id = ExecutionId(0xE2);
    child.attempt_id = AttemptId(0x1);
    child.attempt_generation = AttemptGeneration(1);
    child.model_id = ModelId(0x7);
    child.model_generation = ModelGeneration(1);
    child.input_states = { StateId(0x1) };
    auto p1 = store.publish(child);
    CHECK(p1.ok);

    CHECK(store.size() == 2);
    CHECK(store.ancestors(StateId(0x2)).size() == 1);
    CHECK(store.descendants(StateId(0x1)).size() == 1);
    CHECK(store.by_model(ModelId(0x7)).size() == 2);

    // reuse: match
    ReuseRequest req;
    req.model_id = ModelId(0x7);
    req.model_generation = ModelGeneration(1);
    auto dec = store.check_reuse(StateId(0x2), req);
    CHECK(dec.eligibility == ReuseEligibility::ELIGIBLE);

    // reuse: mismatch
    ReuseRequest req2;
    req2.model_id = ModelId(0x9);
    req2.model_generation = ModelGeneration(1);
    auto dec2 = store.check_reuse(StateId(0x2), req2);
    CHECK(dec2.eligibility == ReuseEligibility::INCOMPATIBLE);
    CHECK(!dec2.failed_dimensions.empty());

    // invalidation propagates to descendant
    auto inv = store.invalidate(InvalidatingSubject{StateId(0x1), InvalidationReason::OperatorInvalidation, "root"});
    CHECK(inv.ok);
    CHECK(inv.result.affected.size() == 2);
    CHECK(store.validity_of(StateId(0x2)) == ValidityState::INVALIDATED);
    auto dec3 = store.check_reuse(StateId(0x2), req);
    CHECK(dec3.eligibility == ReuseEligibility::INVALIDATED);

    // persistence round-trip
    auto snap = store.snapshot();
    CHECK(!store.store_digest(snap).empty());
    auto bytes = encode_snapshot(snap);
    CHECK(!bytes.empty());
    auto decd = decode_snapshot(bytes.data(), bytes.size());
    CHECK(decd.error.empty() && decd.snapshot.has_value());

    ProvenanceStore restored;
    restored.restore(*decd.snapshot);
    CHECK(restored.size() == 2);
    CHECK(restored.validity_of(StateId(0x2)) == ValidityState::INVALIDATED);

    // explanation
    auto rec = store.find(StateId(0x2));
    CHECK(rec != nullptr);
    std::string json = explain::record_json(rec->d);
    CHECK(!json.empty());
    (void)json;

    std::printf("smoke %s  (failures=%d)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
