// Example 08: independent generation rollover and how old work is fenced out.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    auto d = ex_base(StateId(1), ProvenanceId(2)); s.publish(d);
    std::printf("before rollover: prov_gen=%s state_gen=%s\n",
        s.authority().provenance_generation.to_string().c_str(), s.authority().state_generation.to_string().c_str());
    s.roll_provenance_generation(ProvenanceGeneration(2));
    s.roll_state_generation(StateGeneration(2));
    std::printf("after  rollover: prov_gen=%s state_gen=%s\n",
        s.authority().provenance_generation.to_string().c_str(), s.authority().state_generation.to_string().c_str());
    AuthorityClaim old; old.coordinator_epoch = s.authority().epoch; old.provenance_generation = ProvenanceGeneration(1);
    AuthorityState f; f.set_epoch(s.authority().epoch); f.roll_provenance_generation(ProvenanceGeneration(2));
    auto verdict = f.evaluate_claim(old);
    std::printf("old-gen claim rejected=%d (%s)\n", (int)!verdict.accepted(), verdict.dimension.c_str());
    return 0;
}
