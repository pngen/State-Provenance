// Example 12: distributed mutation authority fencing (in-process demonstration).
// The real multiprocess proof over framed TCP is in tools/ (sp_mp_proof).
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    s.register_boot(WorkerId(0xA), WorkerBootId(0xB1));
    s.begin_attempt(AttemptId(0x7), AttemptGeneration(0x3));
    s.set_dependency_generation(DependencyGeneration(0x9));
    auto d = ex_base(StateId(1), ProvenanceId(2));
    AuthorityClaim claim;
    claim.worker_id = WorkerId(0xA); claim.worker_boot = WorkerBootId(0xB1);
    claim.attempt_id = AttemptId(0x7); claim.attempt_generation = AttemptGeneration(0x3);
    claim.state_generation = StateGeneration(1); claim.provenance_generation = ProvenanceGeneration(1);
    claim.coordinator_epoch = s.epoch();
    claim.dependency_generation = DependencyGeneration(0x9);
    auto p = s.publish(d, claim);
    std::printf("fenced publish ok=%d\n", (int)p.ok);
    s.roll_epoch();
    RecordData d2 = ex_base(StateId(2), ProvenanceId(4));
    auto p2 = s.publish(d2, claim);
    std::printf("after epoch roll: accepted=%d error=%s\n", (int)p2.ok,
        p2.error.empty() ? "(none)" : p2.error.c_str());
    return 0;
}
