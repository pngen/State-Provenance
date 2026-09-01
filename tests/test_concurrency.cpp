// ---------------------------------------------------------------------------
// State Provenance - concurrency / thread-safety tests.
// Concurrent publication, lookup, traversal during mutation, invalidation,
// generation rollover, reuse checks, persistence during reads, snapshots.
// ---------------------------------------------------------------------------
#include <atomic>
#include <thread>
#include <vector>

#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"

#include "framework.hpp"

using namespace stateprovenance;

static RecordData base(StateId sid, ProvenanceId pid) {
    RecordData d;
    d.provenance_id = pid; d.provenance_generation = ProvenanceGeneration(1);
    d.subject_id = sid; d.subject_kind = SubjectKind::TensorState;
    d.state_generation = StateGeneration(1);
    d.producer_id = ProducerId(0xA); d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(0xE1); d.attempt_id = AttemptId(0x1); d.attempt_generation = AttemptGeneration(1);
    d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1);
    d.architecture = "sm_120"; d.dtype = "float16"; d.layout = "rowmajor"; d.shape = "8x8";
    d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
    return d;
}

TEST_CASE(concurrent_publication_and_lookup) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    const int writers = 4, per = 8, readers = 4, rounds = 200;
    std::atomic<int> pub_ok{0};
    std::vector<std::thread> ts;
    for (int w = 0; w < writers; ++w) {
        ts.emplace_back([&, w]() {
            // independent branch: root then chain of children
            StateId root(0x1000 + w * 100);
            auto r = base(root, ProvenanceId(root.get() * 2));
            if (store.publish(r).ok) pub_ok.fetch_add(1);
            //
            for (int i = 1; i <= per; ++i) {
                StateId cur(0x1000 + w * 100 + i);
                auto d = base(cur, ProvenanceId(cur.get() * 2));
                d.input_states = {StateId(0x1000 + w * 100 + i - 1)};
                if (store.publish(d).ok) pub_ok.fetch_add(1);
            }
        });
    }
    for (int r = 0; r < readers; ++r) {
        ts.emplace_back([&, r]() {
            for (int it = 0; it < rounds; ++it) {
                // mixed reads
                for (int w = 0; w < writers; ++w) {
                    StateId root(0x1000 + w * 100);
                    (void)store.find(root);
                    (void)store.descendants(root);
                    (void)store.ancestors(StateId(root.get() + per));
                    (void)store.by_model(ModelId(0x7));
                    ReuseRequest req; req.model_id = ModelId(0x7); req.model_generation = ModelGeneration(1);
                    (void)store.check_reuse(StateId(root.get() + per), req);
                }
                // persistence during reads: snapshot + encode + decode concurrently
                (void)store.store_digest(store.snapshot());
                auto sn = store.snapshot();
                auto b = encode_snapshot(sn);
                auto d = decode_snapshot(b.data(), b.size());
                (void)d;
            }
        });
    }
    for (auto& t : ts) t.join();
    CHECK(pub_ok.load() == writers * (per + 1));
    CHECK(store.size() == std::size_t(writers * (per + 1)));
    for (int w = 0; w < writers; ++w) {
        StateId root(0x1000 + w * 100);
        CHECK(store.descendants(root).size() == std::size_t(per));
    }
}

TEST_CASE(concurrent_invalidation_and_traversal) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    // build one shared chain root -> ... -> leaf
    for (int i = 0; i <= 20; ++i) {
        auto d = base(StateId(0x2000 + i), ProvenanceId((0x2000 + i) * 2));
        if (i > 0) d.input_states = {StateId(0x2000 + i - 1)};
        store.publish(d);
    }
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::thread reader([&]() {
        while (!stop) { (void)store.descendants(StateId(0x2000)); (void)store.validity_of(StateId(0x2010)); ++reads; }
    });
    // invalidate mid-leaf while readers traverse
    for (int i = 0; i < 100; ++i) {
        store.invalidate(InvalidatingSubject{StateId(0x2005), InvalidationReason::DependencyInvalidation, "x"});
    }
    stop.store(true);
    reader.join();
    CHECK(store.validity_of(StateId(0x2005)) == ValidityState::INVALIDATED);
    CHECK(store.validity_of(StateId(0x2010)) == ValidityState::INVALIDATED);   // propagated
    CHECK(reads.load() > 0);
}

TEST_CASE(concurrent_generation_rollover_and_reuse) {
    ProvenanceStore store;
    store.roll_provenance_generation(ProvenanceGeneration(1));
    store.roll_state_generation(StateGeneration(1));
    store.set_model_generation(ModelGeneration(1));
    auto d = base(StateId(0x3000), ProvenanceId(0x6000));
    store.publish(d);
    std::atomic<int> checks{0};
    std::thread roller([&]() {
        for (int g = 2; g <= 50; ++g) store.set_model_generation(ModelGeneration(g));
    });
    std::vector<std::thread> checkers;
    for (int c = 0; c < 4; ++c) checkers.emplace_back([&]() {
        for (int it = 0; it < 200; ++it) {
            ReuseRequest req; req.model_id = ModelId(0x7); req.model_generation = ModelGeneration(1);
            auto r = store.check_reuse(StateId(0x3000), req);
            (void)r;
            ++checks;
        }
    });
    roller.join();
    for (auto& t : checkers) t.join();
    CHECK(checks.load() > 0);
}
