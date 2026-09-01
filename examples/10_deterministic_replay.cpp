// Example 10: deterministic serialization/replay reproduces an identical digest.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    for (int i = 1; i <= 5; ++i) {
        auto d = ex_base(StateId(i), ProvenanceId(i*2));
        if (i > 1) d.input_states = { StateId(i-1) };
        s.publish(d);
    }
    auto b1 = encode_snapshot(s.snapshot());
    auto b2 = encode_snapshot(s.snapshot());
    std::printf("encoded identical=%d  digest=%s\n", (int)(b1==b2), s.store_digest(s.snapshot()).c_str());
    auto dec = decode_snapshot(b1.data(), b1.size());
    ProvenanceStore r; r.restore(*dec.snapshot);
    std::printf("replay digest=%s  match=%d\n", r.store_digest(r.snapshot()).c_str(),
        (int)(r.store_digest(r.snapshot())==s.store_digest(s.snapshot())));
    return 0;
}
