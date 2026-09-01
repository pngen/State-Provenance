// ---------------------------------------------------------------------------
// State Provenance - persistence tests.
// Save/load round trip, deterministic digest, corruption, truncation,
// trailing garbage, duplicate-ID rejection, invalid-enum rejection, version
// mismatch, stable recovery digest.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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

static void build_store(ProvenanceStore& s) {
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
    s.set_model_generation(ModelGeneration(1));
    s.set_dependency_generation(DependencyGeneration(1));
    RecordData a = base(StateId(1), ProvenanceId(2));
    RecordData b = base(StateId(2), ProvenanceId(4)); b.input_states = {StateId(1)};
    b.dependencies = { Dependency{DependencyId(0x1), DependencyGeneration(1), "model", "rev1"} };
    RecordData c = base(StateId(3), ProvenanceId(6)); c.input_states = {StateId(1), StateId(2)};
    s.publish(a); s.publish(b); s.publish(c);
}

static std::string tmp_file(const char* name) {
    auto base = std::filesystem::temp_directory_path() / "sp_test";
    std::filesystem::create_directories(base);
    return (base / name).string();
}

TEST_CASE(persistence_round_trip_reproduces_digest) {
    ProvenanceStore s; build_store(s);
    auto sn = s.snapshot();
    auto d0 = s.store_digest(sn);
    std::string path = tmp_file("rt.bin");
    auto err = save_file(sn, path);
    CHECK(!err.has_value());
    CHECK(std::filesystem::exists(path));
    auto lr = load_file(path);
    CHECK(lr.error.empty());
    CHECK(lr.snapshot.has_value());
    if (lr.snapshot) {
        ProvenanceStore r;
        r.restore(*lr.snapshot);
        CHECK(r.size() == 3);
        CHECK(r.store_digest(r.snapshot()) == d0);
        CHECK(r.validity_of(StateId(1)) == ValidityState::VALID);
    }
    std::filesystem::remove(path);
}

TEST_CASE(persistence_is_deterministic) {
    ProvenanceStore s; build_store(s);
    auto b1 = encode_snapshot(s.snapshot());
    auto b2 = encode_snapshot(s.snapshot());
    CHECK(b1 == b2);
    // decode reproduces the same digest
    auto dec = decode_snapshot(b1.data(), b1.size());
    CHECK(dec.error.empty());
    CHECK(dec.snapshot.has_value());
    if (dec.snapshot) { ProvenanceStore r; r.restore(*dec.snapshot); CHECK(r.store_digest(r.snapshot()) == s.store_digest(s.snapshot())); }
}

TEST_CASE(persistence_rejects_corruption) {
    ProvenanceStore s; build_store(s);
    auto b = encode_snapshot(s.snapshot());
    // flip a byte in the payload region
    std::size_t flip = b.size() / 2;
    b[flip] = static_cast<std::byte>(static_cast<unsigned char>(b[flip]) ^ 0xFF);
    auto dec = decode_snapshot(b.data(), b.size());
    CHECK(dec.error.empty() == false);
    CHECK(!dec.snapshot.has_value());
}

TEST_CASE(persistence_rejects_truncation_and_trailing_garbage_and_version) {
    ProvenanceStore s; build_store(s);
    auto b = encode_snapshot(s.snapshot());
    auto trunc = decode_snapshot(b.data(), b.size() / 2);
    CHECK(!trunc.snapshot.has_value());

    std::vector<std::byte> extra = b;
    extra.push_back(std::byte{0xAA});
    auto tail = decode_snapshot(extra.data(), extra.size());
    CHECK(!tail.snapshot.has_value());

    // version mismatch in the payload header
    auto badver = b;
    // payload starts with magic u64 then version u32 at offset 8
    badver[8] = std::byte{0xFF};  // tamper version little-endian byte 0
    auto vd = decode_snapshot(badver.data(), badver.size());
    CHECK(!vd.snapshot.has_value());
}

TEST_CASE(persistence_rejects_malformed_file) {
    auto path = tmp_file("bad.bin");
    { std::ofstream f(path, std::ios::binary); f << "GARBAGE"; }
    auto lr = load_file(path);
    CHECK(!lr.error.empty());
    CHECK(!lr.snapshot.has_value());
    // too small
    { std::ofstream f(path, std::ios::binary); f << "AB"; }
    auto lr2 = load_file(path);
    CHECK(!lr2.snapshot.has_value());
    std::filesystem::remove(path);
}

TEST_CASE(persistence_rejects_duplicate_provenance_id) {
    // Craft a snapshot stream with two records sharing a provenance_id.
    StoreSnapshot s;
    RecordData a = base(StateId(1), ProvenanceId(2));
    RecordData b = base(StateId(2), ProvenanceId(2));   // duplicate provenance id
    s.records.push_back(std::make_shared<const ProvenanceRecord>(a));
    s.records.push_back(std::make_shared<const ProvenanceRecord>(b));
    auto bytes = encode_snapshot(s);
    auto dec = decode_snapshot(bytes.data(), bytes.size());
    CHECK(!dec.snapshot.has_value());
    CHECK(dec.error.find("duplicate") != std::string::npos);
}

TEST_CASE(persistence_rejects_invalid_enum) {
    StoreSnapshot s;
    RecordData a = base(StateId(1), ProvenanceId(2));
    a.subject_kind = static_cast<SubjectKind>(0xFF);  // invalid enum byte
    s.records.push_back(std::make_shared<const ProvenanceRecord>(a));
    auto bytes = encode_snapshot(s);
    auto dec = decode_snapshot(bytes.data(), bytes.size());
    CHECK(!dec.snapshot.has_value());
    CHECK(dec.error.find("enum") != std::string::npos);
}

TEST_CASE(persistence_recovers_overlay_status) {
    ProvenanceStore s; build_store(s);
    s.invalidate(InvalidatingSubject{StateId(1), InvalidationReason::DependencyInvalidation, "z"});
    auto sn = s.snapshot();
    auto dec = decode_snapshot(encode_snapshot(sn).data() , encode_snapshot(sn).size());
    CHECK(dec.error.empty() && dec.snapshot.has_value());
    if (dec.snapshot) {
        ProvenanceStore r; r.restore(*dec.snapshot);
        CHECK(r.validity_of(StateId(1)) == ValidityState::INVALIDATED);
        CHECK(r.validity_of(StateId(2)) == ValidityState::INVALIDATED);  // propagated
        CHECK(r.validity_of(StateId(3)) == ValidityState::INVALIDATED);
    }
}
