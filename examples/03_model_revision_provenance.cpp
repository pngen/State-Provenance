// Example 03: model identity/revision/generation provenance and reuse gating.
#include "common.hpp"
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    s.set_model_generation(ModelGeneration(3));
    auto d = ex_base(StateId(1), ProvenanceId(2));
    d.model_id = ModelId(0x42); d.model_generation = ModelGeneration(3); d.model_revision = "llama-3-8b";
    d.tokenizer = "llama3-tok";
    s.publish(d);
    ReuseRequest req;
    req.model_id = ModelId(0x42); req.model_generation = ModelGeneration(3);
    req.model_revision = "llama-3-8b"; req.tokenizer = "llama3-tok";
    auto dec = s.check_reuse(StateId(1), req);
    std::printf("match  -> %s\n", dec.summary().c_str());
    ReuseRequest req2 = req; req2.model_revision = "llama-3-70b";
    auto dec2 = s.check_reuse(StateId(1), req2);
    std::printf("rev mismatch -> %s\n", dec2.summary().c_str());
    return 0;
}
