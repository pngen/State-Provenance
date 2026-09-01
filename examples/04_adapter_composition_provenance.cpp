// Example 04: adapter/composition identity and generation.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    auto d = ex_base(StateId(1), ProvenanceId(2));
    d.adapter_id = AdapterId(0x31); d.adapter_generation = AdapterGeneration(4);
    d.adapter_revision = "loRA-r32"; d.composition_id = CompositionId(0x51);
    d.composition_generation = CompositionGeneration(2); d.composition_fingerprint = "comp:lorA+r32+quant";
    s.publish(d);
    ReuseRequest req;
    req.adapter_id = AdapterId(0x31); req.adapter_generation = AdapterGeneration(4);
    req.adapter_revision = "loRA-r32"; req.composition_fingerprint = "comp:lorA+r32+quant";
    auto dec = s.check_reuse(StateId(1), req);
    std::printf("composition match -> %s\n", dec.summary().c_str());
    ReuseRequest req2 = req; req2.composition_fingerprint = "comp:unrelated";
    std::printf("composition mismatch -> %s\n", s.check_reuse(StateId(1), req2).summary().c_str());
    return 0;
}
