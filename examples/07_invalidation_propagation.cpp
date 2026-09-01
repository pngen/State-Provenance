// Example 07: deterministic invalidation propagation with causal chains.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    s.publish(ex_base(StateId(1), ProvenanceId(2)));
    auto b = ex_base(StateId(2), ProvenanceId(4)); b.input_states = { StateId(1) }; s.publish(b);
    auto c = ex_base(StateId(3), ProvenanceId(6)); c.input_states = { StateId(2) }; s.publish(c);
    s.publish(ex_base(StateId(4), ProvenanceId(8)));   // unrelated branch
    auto inv = s.invalidate(InvalidatingSubject{StateId(1), InvalidationReason::DependencyInvalidation, "dep gone"});
    std::printf("invalidated %zu subject(s):\n", inv.result.affected.size());
    for (auto id : inv.result.affected) std::printf("  %s -> %s\n", id.to_string().c_str(),
        (s.validity_of(id)==ValidityState::INVALIDATED ? "INVALIDATED" : "VALID"));
    std::printf("causal chain to C: ");
    { auto ch = inv.result.chain_for(StateId(3));
      for (auto x : ch.path) std::printf("%s ", x.to_string().c_str()); }
    std::printf("\nunrelated D validity = %s\n",
        (s.validity_of(StateId(4))==ValidityState::VALID ? "VALID" : "INVALIDATED"));
    return 0;
}
