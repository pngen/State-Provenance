// Example 06: reuse eligibility across environment dimensions with exact denials.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    auto d = ex_base(StateId(1), ProvenanceId(2));
    s.publish(d);
    ReuseRequest match; match.model_id = ModelId(0x7); match.model_generation = ModelGeneration(1);
    match.architecture = "sm_120"; match.dtype = "float16";
    std::printf("match -> %s\n", s.check_reuse(StateId(1), match).summary().c_str());
    ReuseRequest arch = match; arch.architecture = "sm_100";
    std::printf("arch mismatch -> %s\n", s.check_reuse(StateId(1), arch).summary().c_str());
    ReuseRequest dtype = match; dtype.dtype = "float32";
    std::printf("dtype mismatch -> %s\n", s.check_reuse(StateId(1), dtype).summary().c_str());
    return 0;
}
