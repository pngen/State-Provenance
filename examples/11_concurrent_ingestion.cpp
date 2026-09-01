// Example 11: concurrent ingestion and lookup under one store.
#include "common.hpp"
#include <thread>
#include <vector>
#include <atomic>
int main() {
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    std::atomic<int> ok{0};
    std::vector<std::thread> ts;
    for (int w = 0; w < 4; ++w) ts.emplace_back([&, w]() {
        for (int i = 0; i < 20; ++i) {
            StateId sid(0x10000 + w*1000 + i);
            auto d = ex_base(sid, ProvenanceId(sid.get()*2));
            if (i > 0) d.input_states = { StateId(0x10000 + w*1000 + i - 1) };
            if (s.publish(d).ok) ok.fetch_add(1);
        }
    });
    std::thread reader([&]() {
        for (int it = 0; it < 500; ++it) { (void)s.by_model(ModelId(0x7)); (void)s.size(); }
    });
    for (auto& t: ts) t.join(); reader.join();
    std::printf("published=%d  size=%zu\n", ok.load(), s.size());
    return 0;
}
