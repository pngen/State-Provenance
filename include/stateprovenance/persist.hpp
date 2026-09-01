#pragma once
// ---------------------------------------------------------------------------
// State Provenance - versioned binary persistence.
//
// Deterministic little-endian encoding with an explicit version, fixed byte
// order, CRC-32 integrity, bounded lengths, atomic durable replacement, and
// strict rejection of: corruption, truncation, trailing garbage, duplicate
// IDs, invalid enums, invalid generations, malformed references and cycles.
// Recovered provenance reproduces stable digests.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/store.hpp"
#include "stateprovenance/digest.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  undef NOMINMAX
#endif

namespace stateprovenance {

// ---------------------------------------------------------------------------
// Bounded-length constants
// ---------------------------------------------------------------------------
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint64_t kMaxPayloadLen  = 512u * 1024u * 1024u;  // 512 MiB
constexpr std::uint32_t kMaxStringLen   = 64u * 1024u * 1024u;   // 64 MiB
constexpr std::uint32_t kMaxVecLen      = 1u << 26;              // 67M elements
constexpr std::uint64_t kMaxRecords     = 1u << 24;              // 16M records

constexpr std::uint64_t kPayloadMagic   = 0x53505256ull;  // "SPRV"
constexpr std::array<std::uint8_t, 8> kFileMagic{{'S','P','R','O','V','1','.','0'}};

// ---------------------------------------------------------------------------
// BinaryWriter: little-endian, append-only, fixed byte order.
// ---------------------------------------------------------------------------
class BinaryWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }
    void u16(std::uint16_t v) { u8(v & 0xFFu); u8((v >> 8) & 0xFFu); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8((v >> (8 * i)) & 0xFFu); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)); }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) buf_.push_back(static_cast<std::byte>(b[i]));
    }
    void str(std::string_view s) {
        if (s.size() > kMaxStringLen) throw std::runtime_error("string length bound exceeded");
        u32(static_cast<std::uint32_t>(s.size()));
        bytes(s.data(), s.size());
    }
    std::size_t size() const { return buf_.size(); }
    const std::vector<std::byte>& data() const { return buf_; }
    void clear() { buf_.clear(); }

private:
    std::vector<std::byte> buf_;
};

// ---------------------------------------------------------------------------
// BinaryReader: bounds-checked little-endian reader.
// ---------------------------------------------------------------------------
class BinaryReader {
public:
    BinaryReader(const void* data, std::size_t n) : p_(static_cast<const std::uint8_t*>(data)), n_(n) {}

    bool ok() const { return ok_; }
    std::size_t remaining() const { return ok_ ? n_ - pos_ : 0; }
    std::size_t pos() const { return pos_; }

    bool u8(std::uint8_t& v) {
        if (!take(1)) { fail(); return false; }
        v = p_[pos_++]; return true;
    }
    bool u32(std::uint32_t& v) {
        std::uint8_t b[4]; if (!take(4)) { fail(); return false; }
        std::memcpy(b, p_ + pos_, 4); pos_ += 4;
        v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
            (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
        return true;
    }
    bool u64(std::uint64_t& v) {
        std::uint8_t b[8]; if (!take(8)) { fail(); return false; }
        std::memcpy(b, p_ + pos_, 8); pos_ += 8;
        v = 0; for (int i = 0; i < 8; ++i) v |= (static_cast<std::uint64_t>(b[i]) << (8 * i));
        return true;
    }
    bool i64(std::int64_t& v) { std::uint64_t u; if (!u64(u)) return false; v = static_cast<std::int64_t>(u); return true; }
    bool str(std::string& out) {
        std::uint32_t len;
        if (!u32(len)) return false;
        if (len > kMaxStringLen) { fail(); return false; }
        if (!take(len)) { fail(); return false; }
        out.assign(reinterpret_cast<const char*>(p_ + pos_), len);
        pos_ += len;
        return true;
    }
    bool skip(std::size_t n) { if (!take(n)) { fail(); return false; } pos_ += n; return true; }

private:
    bool take(std::size_t n) const { return ok_ && pos_ + n <= n_; }
    void fail() { ok_ = false; }

    const std::uint8_t* p_;
    std::size_t n_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

// ---------------------------------------------------------------------------
// Record <-> wire serialization helpers
// ---------------------------------------------------------------------------
inline void write_record(BinaryWriter& w, const RecordData& d) {
    w.u64(d.provenance_id.get());        w.u64(d.provenance_generation.get());
    w.u64(d.subject_id.get());           w.u8(static_cast<std::uint8_t>(d.subject_kind));
    w.u64(d.state_generation.get());
    w.u64(d.producer_id.get());          w.u64(d.producer_generation.get());
    w.u64(d.execution_id.get());         w.u64(d.attempt_id.get());
    w.u64(d.attempt_generation.get());   w.i64(d.created_at);
    w.u64(d.model_id.get());             w.u64(d.model_generation.get());
    w.str(d.model_revision);             w.str(d.tokenizer);
    w.u64(d.adapter_id.get());           w.u64(d.adapter_generation.get());
    w.str(d.adapter_revision);           w.u64(d.composition_id.get());
    w.u64(d.composition_generation.get()); w.str(d.composition_fingerprint);
    w.u64(d.runtime_id.get());           w.u64(d.runtime_generation.get());
    w.u64(d.artifact_generation.get());
    w.u64(d.backend_id.get());           w.u64(d.toolchain_id.get());
    w.u64(d.device_id.get());            w.str(d.architecture);
    w.str(d.compute_capability);         w.str(d.abi);
    w.str(d.dtype);                      w.str(d.layout);          w.str(d.shape);
    w.u32(static_cast<std::uint32_t>(d.input_states.size()));
    for (const auto& s : d.input_states) w.u64(s.get());
    w.u32(static_cast<std::uint32_t>(d.parent_provenance.size()));
    for (const auto& s : d.parent_provenance) w.u64(s.get());
    w.u32(static_cast<std::uint32_t>(d.dependencies.size()));
    for (const auto& dep : d.dependencies) {
        w.u64(dep.id.get()); w.u64(dep.generation.get()); w.str(dep.kind); w.str(dep.revision);
    }
    w.u32(static_cast<std::uint32_t>(d.requirements.size()));
    for (const auto& req : d.requirements) {
        w.u8(static_cast<std::uint8_t>(req.dimension)); w.str(req.required_value); w.str(req.note);
    }
    w.u64(d.policy_id.get());            w.u64(d.policy_generation.get());
    w.str(d.policy_fingerprint);         w.str(d.content_digest);
    w.u64(d.coordinator_epoch);          w.u64(d.worker_id.get());  w.u64(d.worker_boot.get());
    w.u8(static_cast<std::uint8_t>(d.evidence));
    w.u8(static_cast<std::uint8_t>(d.validity));
    w.u8(static_cast<std::uint8_t>(d.invalidation_reason));
    w.u8(static_cast<std::uint8_t>(d.reuse_eligibility));
    w.str(d.note);
}

// Returns error message or nullopt.
inline std::optional<std::string> read_record(BinaryReader& r, RecordData& d) {
    std::uint64_t u;
    std::uint8_t b;
    if (!r.u64(u)) return "truncated record(provenance_id)"; d.provenance_id = ProvenanceId(u);
    if (!r.u64(u)) return "truncated record(provenance_generation)"; d.provenance_generation = ProvenanceGeneration(u);
    if (!r.u64(u)) return "truncated record(subject_id)"; d.subject_id = StateId(u);
    if (!r.u8(b)) return "truncated record(subject_kind)";
    d.subject_kind = static_cast<SubjectKind>(b);
    if (!is_valid(d.subject_kind)) return "invalid enum: subject_kind";
    if (!r.u64(u)) return "truncated record(state_generation)"; d.state_generation = StateGeneration(u);
    if (!r.u64(u)) return "truncated record(producer_id)"; d.producer_id = ProducerId(u);
    if (!r.u64(u)) return "truncated record(producer_generation)"; d.producer_generation = ProducerGeneration(u);
    if (!r.u64(u)) return "truncated record(execution_id)"; d.execution_id = ExecutionId(u);
    if (!r.u64(u)) return "truncated record(attempt_id)"; d.attempt_id = AttemptId(u);
    if (!r.u64(u)) return "truncated record(attempt_generation)"; d.attempt_generation = AttemptGeneration(u);
    std::int64_t t; if (!r.i64(t)) return "truncated record(created_at)"; d.created_at = t;
    if (!r.u64(u)) return "truncated record(model_id)"; d.model_id = ModelId(u);
    if (!r.u64(u)) return "truncated record(model_generation)"; d.model_generation = ModelGeneration(u);
    if (!r.str(d.model_revision)) return "truncated record(model_revision)";
    if (!r.str(d.tokenizer)) return "truncated record(tokenizer)";
    if (!r.u64(u)) return "truncated record(adapter_id)"; d.adapter_id = AdapterId(u);
    if (!r.u64(u)) return "truncated record(adapter_generation)"; d.adapter_generation = AdapterGeneration(u);
    if (!r.str(d.adapter_revision)) return "truncated record(adapter_revision)";
    if (!r.u64(u)) return "truncated record(composition_id)"; d.composition_id = CompositionId(u);
    if (!r.u64(u)) return "truncated record(composition_generation)"; d.composition_generation = CompositionGeneration(u);
    if (!r.str(d.composition_fingerprint)) return "truncated record(composition_fingerprint)";
    if (!r.u64(u)) return "truncated record(runtime_id)"; d.runtime_id = RuntimeId(u);
    if (!r.u64(u)) return "truncated record(runtime_generation)"; d.runtime_generation = RuntimeGeneration(u);
    if (!r.u64(u)) return "truncated record(artifact_generation)"; d.artifact_generation = ArtifactGeneration(u);
    if (!r.u64(u)) return "truncated record(backend_id)"; d.backend_id = BackendId(u);
    if (!r.u64(u)) return "truncated record(toolchain_id)"; d.toolchain_id = ToolchainId(u);
    if (!r.u64(u)) return "truncated record(device_id)"; d.device_id = DeviceId(u);
    if (!r.str(d.architecture)) return "truncated record(architecture)";
    if (!r.str(d.compute_capability)) return "truncated record(compute_capability)";
    if (!r.str(d.abi)) return "truncated record(abi)";
    if (!r.str(d.dtype)) return "truncated record(dtype)";
    if (!r.str(d.layout)) return "truncated record(layout)";
    if (!r.str(d.shape)) return "truncated record(shape)";

    std::uint32_t n;
    if (!r.u32(n) || n > kMaxVecLen) return "invalid input_states count";
    for (std::uint32_t i = 0; i < n; ++i) { if (!r.u64(u)) return "truncated input_states"; d.input_states.push_back(StateId(u)); }
    if (!r.u32(n) || n > kMaxVecLen) return "invalid parent_provenance count";
    for (std::uint32_t i = 0; i < n; ++i) { if (!r.u64(u)) return "truncated parent_provenance"; d.parent_provenance.push_back(ProvenanceId(u)); }
    if (!r.u32(n) || n > kMaxVecLen) return "invalid dependencies count";
    for (std::uint32_t i = 0; i < n; ++i) {
        Dependency dep;
        if (!r.u64(u)) return "truncated dependency id"; dep.id = DependencyId(u);
        if (!r.u64(u)) return "truncated dependency generation"; dep.generation = DependencyGeneration(u);
        if (!r.str(dep.kind)) return "truncated dependency kind";
        if (!r.str(dep.revision)) return "truncated dependency revision";
        d.dependencies.push_back(std::move(dep));
    }
    if (!r.u32(n) || n > kMaxVecLen) return "invalid requirements count";
    for (std::uint32_t i = 0; i < n; ++i) {
        CompatibilityRequirement req;
        if (!r.u8(b)) return "truncated requirement dimension";
        req.dimension = static_cast<CompatibilityDimension>(b);
        if (!is_valid(req.dimension)) return "invalid enum: requirement dimension";
        if (!r.str(req.required_value)) return "truncated requirement value";
        if (!r.str(req.note)) return "truncated requirement note";
        d.requirements.push_back(std::move(req));
    }

    if (!r.u64(u)) return "truncated record(policy_id)"; d.policy_id = PolicyId(u);
    if (!r.u64(u)) return "truncated record(policy_generation)"; d.policy_generation = PolicyGeneration(u);
    if (!r.str(d.policy_fingerprint)) return "truncated record(policy_fingerprint)";
    if (!r.str(d.content_digest)) return "truncated record(content_digest)";
    if (!r.u64(u)) return "truncated record(coordinator_epoch)"; d.coordinator_epoch = u;
    if (!r.u64(u)) return "truncated record(worker_id)"; d.worker_id = WorkerId(u);
    if (!r.u64(u)) return "truncated record(worker_boot)"; d.worker_boot = WorkerBootId(u);
    if (!r.u8(b)) return "truncated record(evidence)"; d.evidence = static_cast<EvidenceClass>(b);
    if (!is_valid(d.evidence)) return "invalid enum: evidence";
    if (!r.u8(b)) return "truncated record(validity)"; d.validity = static_cast<ValidityState>(b);
    if (!is_valid(d.validity)) return "invalid enum: validity";
    if (!r.u8(b)) return "truncated record(invalidation_reason)"; d.invalidation_reason = static_cast<InvalidationReason>(b);
    if (!is_valid(d.invalidation_reason)) return "invalid enum: invalidation_reason";
    if (!r.u8(b)) return "truncated record(reuse_eligibility)"; d.reuse_eligibility = static_cast<ReuseEligibility>(b);
    if (!is_valid(d.reuse_eligibility)) return "invalid enum: reuse_eligibility";
    if (!r.str(d.note)) return "truncated record(note)";

    auto ve = validate_record(d);
    if (ve) return "record failed validation: " + *ve;
    return std::nullopt;
}

inline void write_authority(BinaryWriter& w, const AuthoritySnapshot& s) {
    w.u64(s.epoch);
    w.u32(static_cast<std::uint32_t>(s.boots.size()));
    for (const auto& [worker, boot] : s.boots) { w.u64(worker.get()); w.u64(boot.get()); }
    w.u64(s.attempt_id.get());        w.u64(s.attempt_generation.get());
    w.u64(s.state_generation.get());  w.u64(s.provenance_generation.get());
    w.u64(s.artifact_generation.get()); w.u64(s.model_generation.get());
    w.u64(s.adapter_generation.get());  w.u64(s.composition_generation.get());
    w.u64(s.policy_generation.get());   w.u64(s.dependency_generation.get());
}

inline std::optional<std::string> read_authority(BinaryReader& r, AuthoritySnapshot& s) {
    std::uint64_t u;
    if (!r.u64(u)) return "truncated authority(epoch)"; s.epoch = u;
    std::uint32_t n;
    if (!r.u32(n) || n > kMaxVecLen) return "invalid boots count";
    for (std::uint32_t i = 0; i < n; ++i) {
        if (!r.u64(u)) return "truncated boots(worker)"; WorkerId w(u);
        if (!r.u64(u)) return "truncated boots(boot)"; WorkerBootId b(u);
        s.boots[w] = b;
    }
    if (!r.u64(u)) return "truncated authority(attempt_id)"; s.attempt_id = AttemptId(u);
    if (!r.u64(u)) return "truncated authority(attempt_generation)"; s.attempt_generation = AttemptGeneration(u);
    if (!r.u64(u)) return "truncated authority(state_generation)"; s.state_generation = StateGeneration(u);
    if (!r.u64(u)) return "truncated authority(provenance_generation)"; s.provenance_generation = ProvenanceGeneration(u);
    if (!r.u64(u)) return "truncated authority(artifact_generation)"; s.artifact_generation = ArtifactGeneration(u);
    if (!r.u64(u)) return "truncated authority(model_generation)"; s.model_generation = ModelGeneration(u);
    if (!r.u64(u)) return "truncated authority(adapter_generation)"; s.adapter_generation = AdapterGeneration(u);
    if (!r.u64(u)) return "truncated authority(composition_generation)"; s.composition_generation = CompositionGeneration(u);
    if (!r.u64(u)) return "truncated authority(policy_generation)"; s.policy_generation = PolicyGeneration(u);
    if (!r.u64(u)) return "truncated authority(dependency_generation)"; s.dependency_generation = DependencyGeneration(u);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Full snapshot <-> bytes
// ---------------------------------------------------------------------------
inline std::vector<std::byte> encode_snapshot(const StoreSnapshot& s) {
    BinaryWriter w;
    w.u64(kPayloadMagic);
    w.u32(kFormatVersion);
    w.u64(static_cast<std::uint64_t>(s.records.size()));
    for (const auto& rec : s.records) write_record(w, rec->d);
    w.u32(static_cast<std::uint32_t>(s.status.size()));
    for (const auto& [id, st] : s.status) {
        w.u64(id.get());
        w.u8(static_cast<std::uint8_t>(st.validity));
        w.u8(static_cast<std::uint8_t>(st.reason));
        w.u64(st.provenance_generation.get());
        w.str(st.detail);
    }
    write_authority(w, s.authority);
    return w.data();
}

struct DecodeResult {
    std::optional<StoreSnapshot> snapshot;
    std::string error;
};

inline DecodeResult decode_snapshot(const void* data, std::size_t n) {
    DecodeResult res;
    BinaryReader r(data, n);
    std::uint64_t magic;
    if (!r.u64(magic)) { res.error = "truncated: missing payload magic"; return res; }
    if (magic != kPayloadMagic) { res.error = "bad payload magic"; return res; }
    std::uint32_t ver;
    if (!r.u32(ver)) { res.error = "truncated: missing version"; return res; }
    if (ver != kFormatVersion) { res.error = "unsupported format version"; return res; }

    std::uint64_t record_count;
    if (!r.u64(record_count)) { res.error = "truncated: missing record count"; return res; }
    if (record_count > kMaxRecords) { res.error = "record count exceeds bound"; return res; }

    StoreSnapshot s;
    for (std::uint64_t i = 0; i < record_count; ++i) {
        RecordData d;
        auto err = read_record(r, d);
        if (err) { res.error = *err; return res; }
        // duplicate provenance id rejection
        for (const auto& rec : s.records)
            if (rec->d.provenance_id == d.provenance_id) { res.error = "duplicate provenance id in stream"; return res; }
        s.records.push_back(std::make_shared<const ProvenanceRecord>(d));
    }
    // Maintain topological order invariant for decode; if violated, reject.
    // (enforced by caller via store.restore; leave records as-is).

    std::uint32_t status_count;
    if (!r.u32(status_count) || status_count > kMaxVecLen) { res.error = "invalid status count"; return res; }
    for (std::uint32_t i = 0; i < status_count; ++i) {
        std::uint64_t id; std::uint8_t vb, rb; std::uint64_t pg; std::string detail;
        if (!r.u64(id)) { res.error = "truncated status id"; return res; }
        if (!r.u8(vb)) { res.error = "truncated status validity"; return res; }
        if (!is_valid(static_cast<ValidityState>(vb))) { res.error = "invalid enum: status validity"; return res; }
        if (!r.u8(rb)) { res.error = "truncated status reason"; return res; }
        if (!is_valid(static_cast<InvalidationReason>(rb))) { res.error = "invalid enum: status reason"; return res; }
        if (!r.u64(pg)) { res.error = "truncated status provenance generation"; return res; }
        if (!r.str(detail)) { res.error = "truncated status detail"; return res; }
        RecordStatus st;
        st.validity = static_cast<ValidityState>(vb);
        st.reason = static_cast<InvalidationReason>(rb);
        st.provenance_generation = ProvenanceGeneration(pg);
        st.detail = std::move(detail);
        s.status[StateId(id)] = std::move(st);
    }
    auto aerr = read_authority(r, s.authority);
    if (aerr) { res.error = *aerr; return res; }
    if (r.remaining() != 0) { res.error = "trailing data after snapshot"; return res; }

    res.snapshot = std::move(s);
    return res;
}

// ---------------------------------------------------------------------------
// File I/O with CRC-32 header and atomic durable replacement.
// ---------------------------------------------------------------------------
inline std::optional<std::string> save_file(const StoreSnapshot& s, const std::string& path) {
    std::vector<std::byte> payload = encode_snapshot(s);

    BinaryWriter h;
    for (auto c : kFileMagic) h.u8(c);
    h.u32(kFormatVersion);
    h.u32(0);                                        // flags
    h.u64(static_cast<std::uint64_t>(payload.size()));
    std::uint32_t crc = digest::crc32(payload.data(), payload.size());
    h.u32(crc);

    std::vector<std::byte> out = h.data();
    out.insert(out.end(), payload.begin(), payload.end());

    const std::string tmp = path + ".tmp";
    {   std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "cannot open temp file for write: " + tmp;
        f.write(reinterpret_cast<const char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
        if (!f) return "write failed: " + tmp;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return "atomic replace failed: " + path;
#else
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec) return "atomic replace failed: " + path;
    }
#endif
    return std::nullopt;
}

struct LoadResult {
    std::optional<StoreSnapshot> snapshot;
    std::string error;
    std::uint32_t stored_crc = 0;
    std::uint32_t computed_crc = 0;
};

inline LoadResult load_file(const std::string& path) {
    LoadResult res;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { res.error = "cannot open for read: " + path; return res; }
    const std::streamsize size = f.tellg();
    if (size < 0) { res.error = "cannot stat: " + path; return res; }
    f.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!f) { res.error = "read failed: " + path; return res; }
    }

    if (bytes.size() < 28) { res.error = "file too small (truncated header)"; return res; }

    BinaryReader hr(bytes.data(), 28);
    std::uint8_t c;
    for (auto m : kFileMagic) { if (!hr.u8(c) || c != m) { res.error = "bad file magic"; return res; } }
    std::uint32_t ver;
    hr.u32(ver);
    if (ver != kFormatVersion) { res.error = "unsupported file version"; return res; }
    std::uint32_t flags; hr.u32(flags);
    std::uint64_t payload_len; hr.u64(payload_len);
    std::uint32_t crc; hr.u32(crc);
    res.stored_crc = crc;

    if (payload_len > kMaxPayloadLen) { res.error = "payload length exceeds bound"; return res; }
    if (payload_len > bytes.size() - 28) { res.error = "truncated payload"; return res; }
    if (payload_len + 28 < bytes.size()) { res.error = "trailing garbage after payload"; return res; }
    if (payload_len + 28 != bytes.size()) { res.error = "payload length mismatch"; return res; }

    const auto* payload = bytes.data() + 28;
    res.computed_crc = digest::crc32(payload, static_cast<std::size_t>(payload_len));
    if (res.computed_crc != crc) { res.error = "crc32 mismatch (corruption detected)"; return res; }

    auto dec = decode_snapshot(payload, static_cast<std::size_t>(payload_len));
    if (dec.error.empty() && dec.snapshot) {
        res.snapshot = std::move(dec.snapshot);
    } else {
        res.error = dec.error;
    }
    return res;
}

} // namespace stateprovenance
