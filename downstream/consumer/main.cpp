#include <cstdio>
#include "stateprovenance/store.hpp"
int main() {
    using namespace stateprovenance;
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    RecordData d;
    d.provenance_id = ProvenanceId(0x10);
    d.provenance_generation = ProvenanceGeneration(1);
    d.subject_id = StateId(0x1);
    d.subject_kind = SubjectKind::KVState;
    d.state_generation = StateGeneration(1);
    d.producer_id = ProducerId(0xA);
    d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(0xE1);
    d.attempt_id = AttemptId(0x1);
    d.attempt_generation = AttemptGeneration(1);
    d.model_id = ModelId(0x7);
    d.model_generation = ModelGeneration(1);
    d.evidence = EvidenceClass::MEASURED;
    auto p = s.publish(d);
    ReuseRequest req;
    req.model_id = ModelId(0x7);
    req.model_generation = ModelGeneration(1);
    auto dec = s.check_reuse(StateId(0x1), req);
    std::printf("downstream consumer: published=%d size=%zu reuse=%s\n",
                (int)p.ok, s.size(), dec.summary().c_str());
    return (p.ok && dec.eligibility == ReuseEligibility::ELIGIBLE) ? 0 : 1;
}