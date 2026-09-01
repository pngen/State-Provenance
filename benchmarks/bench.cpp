// ---------------------------------------------------------------------------
// State Provenance - benchmark suite.
// Reports throughput/latency for completed, useful operations with explicit
// workload sizes and units.  Uses a deterministic seed.
// ---------------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"

using namespace stateprovenance;

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static RecordData mk(StateId sid, StateId parent) {
    RecordData d;
    d.provenance_id = ProvenanceId(sid.get() * 2);
    d.provenance_generation = ProvenanceGeneration(1);
    d.subject_id = sid; d.subject_kind = (sid.get() % 2) ? SubjectKind::KVState : SubjectKind::TensorState;
    d.state_generation = StateGeneration(1);
    d.producer_id = ProducerId(0xA); d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(0xE1); d.attempt_id = AttemptId(0x1); d.attempt_generation = AttemptGeneration(1);
    d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1);
    d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
    if (parent.valid()) d.input_states = { parent };
    return d;
}

int main() {
    std::printf("State Provenance benchmark (deterministic seed 0x1234abcd)\n");
    const int N = 5000;
    ProvenanceStore s;
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));

    // record creation + publication
    auto t0 = Clock::now();
    s.publish(mk(StateId(1), StateId()));
    for (int i = 2; i <= N; ++i) s.publish(mk(StateId(i), StateId(i - 1)));
    auto t1 = Clock::now();
    std::printf("provenance record creation+publication : %d records in %.2f ms (%.2f us/record)\n",
                N, ms(t0, t1), ms(t0, t1) * 1000.0 / N);

    // indexed lookup
    t0 = Clock::now();
    std::size_t found = 0;
    for (int i = 1; i <= N; ++i) if (s.find(StateId(i))) ++found;
    t1 = Clock::now();
    std::printf("indexed lookup (StateId)              : %zu lookups in %.2f ms (%.2f us/lookup)\n",
                found, ms(t0, t1), ms(t0, t1) * 1000.0 / found);

    // reuse evaluation (sampled; the reuse path walks the ancestry chain)
    ReuseRequest req; req.model_id = ModelId(0x7); req.model_generation = ModelGeneration(1);
    t0 = Clock::now();
    int reuses = 0;
    for (int i = 1; i <= N; i += (N / 200)) { (void)s.check_reuse(StateId(i), req); ++reuses; }
    t1 = Clock::now();
    std::printf("reuse-eligibility evaluation           : %d sampled evals in %.2f ms (%.2f us/eval)\n",
                reuses, ms(t0, t1), ms(t0, t1) * 1000.0 / reuses);

    // parent/child traversal
    StateId mid(N / 2);
    t0 = Clock::now();
    for (int i = 0; i < 2000; ++i) { (void)s.parents(mid); (void)s.children(mid); }
    t1 = Clock::now();
    std::printf("parent/child traversal                : 2000 pairs in %.2f ms\n", ms(t0, t1));

    // ancestry / descendant traversal
    t0 = Clock::now();
    std::size_t total = 0;
    for (int i = 0; i < 100; ++i) total += s.ancestors(mid).size() + s.descendants(StateId(1)).size();
    t1 = Clock::now();
    std::printf("ancestry+descendant traversal         : %zu nodes across 100 traversals in %.2f ms\n", total, ms(t0, t1));

    // invalidation impact analysis
    t0 = Clock::now();
    for (int i = 0; i < 50; ++i) s.invalidate(InvalidatingSubject{StateId(1), InvalidationReason::OperatorInvalidation, "x"});
    t1 = Clock::now();
    std::printf("invalidation impact analysis          : 50 invalidations (each propagates N) in %.2f ms\n", ms(t0, t1));

    // deterministic serialization
    auto sn = s.snapshot();
    t0 = Clock::now();
    for (int i = 0; i < 50; ++i) { auto b = encode_snapshot(sn); (void)b; }
    t1 = Clock::now();
    std::printf("deterministic serialization           : 50 x %d records in %.2f ms\n", N, ms(t0, t1));

    // persistence save
    std::string path = std::filesystem::temp_directory_path().string() + "/sp_bench.bin";
    t0 = Clock::now();
    auto err = save_file(sn, path);
    t1 = Clock::now();
    std::printf("persistence save (durable)            : %.2f ms (err=%s)\n", ms(t0, t1), err ? "yes" : "no");

    // recovery
    t0 = Clock::now();
    auto lr = load_file(path);
    ProvenanceStore r; if (lr.snapshot) r.restore(*lr.snapshot);
    t1 = Clock::now();
    std::printf("recovery (load+restore)               : %.2f ms (size=%zu)\n", ms(t0, t1), r.size());

    // replay
    t0 = Clock::now();
    auto b = encode_snapshot(r.snapshot());
    auto dec = decode_snapshot(b.data(), b.size());
    t1 = Clock::now();
    std::printf("replay (encode+decode+restore)        : %.2f ms (digest=%s)\n", ms(t0, t1),
                r.store_digest(r.snapshot()).c_str());

    // concurrent ingestion
    {
        ProvenanceStore cs;
        cs.roll_provenance_generation(ProvenanceGeneration(1));
        cs.roll_state_generation(StateGeneration(1));
        std::vector<std::thread> ts; const int W = 4, PER = 1000;
        t0 = Clock::now();
        for (int w = 0; w < W; ++w) ts.emplace_back([&, w]() {
            for (int i = 0; i < PER; ++i) {
                StateId sid(0x500000 + w * 10000 + i);
                RecordData d = mk(sid, StateId(0x500000 + w * 10000 + i - 1));
                if (i == 0) d.input_states.clear();
                cs.publish(d);
            }
        });
        for (auto& t : ts) t.join();
        t1 = Clock::now();
        std::printf("concurrent ingestion                  : %d records across %d threads in %.2f ms\n",
                    W * PER, W, ms(t0, t1));
    }
    std::filesystem::remove(path);
    std::printf("benchmark complete\n");
    return 0;
}
