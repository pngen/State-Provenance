// Example 02: a diamond/merged derivation with shared ancestry.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    s.publish(ex_base(StateId(1), ProvenanceId(2)));
    s.publish(ex_base(StateId(2), ProvenanceId(4)));
    auto c = ex_base(StateId(3), ProvenanceId(6)); c.input_states = { StateId(1), StateId(2) };
    s.publish(c);
    auto sh = s.shared_ancestors(StateId(3), StateId(2));
    std::printf("shared ancestors of C and B:\n");
    for (auto a : sh) std::printf("  %s\n", a.to_string().c_str());
    std::printf("fan-in of C = %zu; fan-out of A = %zu\n", s.parents(StateId(3)).size(), s.children(StateId(1)).size());
    return 0;
}
