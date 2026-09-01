// Example 09: durable persist / recover with a stable digest.
#include "common.hpp"
#include <filesystem>
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    s.publish(ex_base(StateId(1), ProvenanceId(2)));
    auto b = ex_base(StateId(2), ProvenanceId(4)); b.input_states = { StateId(1) }; s.publish(b);
    std::string path = std::filesystem::temp_directory_path().string() + "/sp_ex09.bin";
    auto d0 = s.store_digest(s.snapshot());
    auto err = save_file(s.snapshot(), path);
    std::printf("save err=%s digest=%s\n", err ? err->c_str() : "(none)", d0.c_str());
    auto lr = load_file(path);
    ProvenanceStore r; r.restore(*lr.snapshot);
    std::printf("recovered size=%zu digest=%s match=%d\n", r.size(), r.store_digest(r.snapshot()).c_str(),
        (int)(r.store_digest(r.snapshot())==d0));
    std::filesystem::remove(path);
    return 0;
}
