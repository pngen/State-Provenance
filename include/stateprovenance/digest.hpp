#pragma once
// ---------------------------------------------------------------------------
// State Provenance - deterministic hashing and integrity checking.
// - crc32:      standard reflected CRC-32 (IEEE 802.3 / ITU-T V.42), bitwise
//               and table-based, fully deterministic.
// - stable_hash: 64-bit FNV-1a then splitmix64 finalizer, deterministic.
// - StableDigest: a typed wrapper that composes many inputs into one digest.
// ---------------------------------------------------------------------------
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "stateprovenance/ids.hpp"

namespace stateprovenance::digest {

// ---------------------------------------------------------------------------
// CRC-32
// ---------------------------------------------------------------------------
constexpr std::uint32_t crc_table_make(std::size_t i) {
    std::uint32_t c = static_cast<std::uint32_t>(i);
    for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
    return c;
}

inline const std::array<std::uint32_t, 256>& crc_table() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::size_t i = 0; i < 256; ++i) t[i] = crc_table_make(i);
        return t;
    }();
    return table;
}

inline std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t crc = 0xFFFFFFFFu) {
    const auto& t = crc_table();
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i)
        crc = t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline std::uint32_t crc32(std::string_view sv) {
    return crc32(sv.data(), sv.size());
}

// ---------------------------------------------------------------------------
// FNV-1a 64 + splitmix64 finalizer
// ---------------------------------------------------------------------------
inline constexpr std::uint64_t fnv1a64(const void* data, std::size_t len, std::uint64_t hash = 0xcbf29ce484222325ull) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint64_t>(p[i]);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

inline constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

inline std::uint64_t stable_hash(std::string_view sv) {
    return mix64(fnv1a64(sv.data(), sv.size()));
}

// Mix a uint64/a string into a running digest deterministically.
inline void mix(std::uint64_t& h, std::uint64_t v) noexcept {
    h ^= mix64(v);
    h *= 0x9e3779b97f4a7c15ull;
}
inline void mix(std::uint64_t& h, std::string_view sv) noexcept {
    h ^= mix64(fnv1a64(sv.data(), sv.size()));
    h *= 0x9e3779b97f4a7c15ull;
}
inline void mix(std::uint64_t& h, bool v) noexcept { mix(h, v ? 1ull : 0ull); }
inline void mix(std::uint64_t& h, int i) noexcept { mix(h, static_cast<std::uint64_t>(i)); }
inline void mix(std::uint64_t& h, std::uint32_t v) noexcept { mix(h, static_cast<std::uint64_t>(v)); }

inline std::string digest_string(std::uint64_t h) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// StableDigest: a stateful, deterministic, order-sensitive digest builder.
// ---------------------------------------------------------------------------
class StableDigest {
public:
    StableDigest() noexcept = default;
    explicit StableDigest(std::uint64_t seed) noexcept : h_(seed) {}

    StableDigest& with(std::uint64_t v) noexcept { mix(h_, v); return *this; }
    StableDigest& with(bool v) noexcept { mix(h_, v); return *this; }
    StableDigest& with(int v) noexcept { mix(h_, static_cast<std::uint64_t>(v)); return *this; }
    StableDigest& with(std::uint32_t v) noexcept { mix(h_, static_cast<std::uint64_t>(v)); return *this; }
    StableDigest& with(std::int64_t v) noexcept { mix(h_, static_cast<std::uint64_t>(v)); return *this; }
    StableDigest& with(std::string_view sv) noexcept { mix(h_, sv); return *this; }
    StableDigest& with(const char* s) noexcept { if (s) mix(h_, std::string_view(s)); else mix(h_, 0ull); return *this; }
    // Identifier/generation-like types (ids.hpp) hash their numeric value.
    template <typename Tag>
    StableDigest& with(const ScalarIdentifier<Tag>& v) noexcept { mix(h_, v.get()); return *this; }

    std::uint64_t value() const noexcept { return h_; }
    std::string str() const { return digest_string(h_); }
    void reset(std::uint64_t seed = 0) noexcept { h_ = seed; }

private:
    std::uint64_t h_{0};
};

} // namespace stateprovenance::digest
