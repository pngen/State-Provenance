// Example 05: a compiled kernel / generated state provenance chain.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    auto input = ex_base(StateId(1), ProvenanceId(2));
    input.subject_kind = SubjectKind::Buffer;
    auto kernel = ex_base(StateId(2), ProvenanceId(4));
    kernel.subject_kind = SubjectKind::CompiledKernel;
    kernel.dtype = "float16"; kernel.architecture = "sm_120"; kernel.compute_capability = "12.0";
    auto out = ex_base(StateId(3), ProvenanceId(6));
    out.subject_kind = SubjectKind::TensorState;
    out.input_states = { StateId(1) };
    out.dependencies = { Dependency{ DependencyId(0x1), DependencyGeneration(1), "compiled-kernel", "0x2" } };
    s.publish(input); s.publish(kernel); auto p = s.publish(out);
    std::printf("kernel state produced=%d size=%zu\n", (int)p.ok, s.size());
    std::printf("dependencies of output state:\n");
    for (auto& dep : s.find(StateId(3))->d.dependencies)
        std::printf("  %s (%s) rev %s\n", dep.id.to_string().c_str(), dep.kind.c_str(), dep.revision.c_str());
    return 0;
}
