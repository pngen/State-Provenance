// Example 13: provenance for GPU/kernel-generated state.
// Host program records the provenance of a compiled CUDA kernel's derived state;
// the REAL hardware-backed proof is sp_cuda_proof (runs on RTX 5090 / sm_120).
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    auto in = ex_base(StateId(1), ProvenanceId(2)); in.subject_kind = SubjectKind::Buffer; s.publish(in);
    auto kernel = ex_base(StateId(2), ProvenanceId(4));
    kernel.subject_kind = SubjectKind::CompiledKernel; kernel.model_id = ModelId(0x6001);
    kernel.architecture = "sm_120"; kernel.compute_capability = "12.0";
    kernel.backend_id = BackendId(0x6101); kernel.runtime_id = RuntimeId(0x6201);
    kernel.toolchain_id = ToolchainId(0x6301); kernel.device_id = DeviceId(0x6401);
    s.publish(kernel);
    auto out = ex_base(StateId(3), ProvenanceId(6));
    out.subject_kind = SubjectKind::TensorState; out.input_states = { StateId(1) };
    out.model_id = ModelId(0x6001); out.architecture = "sm_120"; out.compute_capability = "12.0";
    out.dtype = "float16"; out.content_digest = "crc32:deadbeef";
    out.dependencies = { Dependency{ DependencyId(0x9), DependencyGeneration(1), "compiled-kernel", "0x2" } };
    auto p = s.publish(out);
    std::printf("CUDA kernel-derived state published=%d\n", (int)p.ok);
    std::printf("device: %s cc %s  backend=%s runtime=%s toolchain=%s\n",
        kernel.architecture.c_str(), kernel.compute_capability.c_str(),
        kernel.backend_id.to_string().c_str(), kernel.runtime_id.to_string().c_str(),
        kernel.toolchain_id.to_string().c_str());
    std::printf("run the real proof with: sp_cuda_proof  (validated on RTX 5090 / sm_120)\n");
    return 0;
}
