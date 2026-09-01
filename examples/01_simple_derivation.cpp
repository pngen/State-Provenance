// Example 01: a minimal, valid derivation (root -> derived child).
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    auto root = ex_base(StateId(1), ProvenanceId(2));
    auto child = ex_base(StateId(2), ProvenanceId(4));
    child.subject_kind = SubjectKind::TensorState;
    child.input_states = { StateId(1) };
    s.publish(root);
    auto p = s.publish(child);
    std::printf("derivation ok=%d  size=%zu\n", (int)p.ok, s.size());
    std::printf("ancestry of 2:\n");
    for (auto a : s.ancestors(StateId(2))) std::printf("  <- %s\n", a.to_string().c_str());
    std::printf("descendants of 1:\n");
    for (auto d : s.descendants(StateId(1))) std::printf("  -> %s\n", d.to_string().c_str());
    return 0;
}
