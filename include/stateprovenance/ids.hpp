#pragma once
// ---------------------------------------------------------------------------
// State Provenance - Strong typed identifiers and independently rolled
// generations.  A shared, strongly-typed scalar template backs every identity
// and generation domain.  Each use is a DISTINCT type: StateId and ModelId are
// not interchangeable; StateGeneration and ProvenanceGeneration are not
// interchangeable.  IDs/generations are never raw integers or loose strings.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <compare>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace stateprovenance {

// ---------------------------------------------------------------------------
// ScalarIdentifier<Tag>
// A 64-bit value with a type tag.  Value 0 is the null/invalid sentinel.
// ---------------------------------------------------------------------------
template <typename Tag>
struct ScalarIdentifier {
    using value_type = std::uint64_t;

    value_type value{0};

    constexpr ScalarIdentifier() noexcept = default;
    constexpr explicit ScalarIdentifier(value_type v) noexcept : value(v) {}
    constexpr ScalarIdentifier(std::nullptr_t) noexcept : value(0) {}

    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool is_null() const noexcept { return value == 0; }

    constexpr value_type get() const noexcept { return value; }

    // Deterministic text form; always 0x-prefixed hex, zero-padded to 16.
    std::string to_string() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(value));
        return std::string(buf);
    }

    // Parse a decimal or 0x-prefixed hex string.  Empty/malformed -> nullopt.
    static std::optional<ScalarIdentifier> try_parse(std::string_view s) {
        if (s.empty()) return std::nullopt;
        std::uint64_t v = 0;
        const bool hex = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
        const char* begin = hex ? s.data() + 2 : s.data();
        const std::size_t n = s.size() - (hex ? 2u : 0u);
        if (n == 0) return std::nullopt;
        const std::uint64_t base = hex ? 16u : 10u;
        for (std::size_t i = 0; i < n; ++i) {
            const char c = begin[i];
            std::uint64_t d = 0;
            if (c >= '0' && c <= '9') d = static_cast<std::uint64_t>(c - '0');
            else if (hex && c >= 'a' && c <= 'f') d = static_cast<std::uint64_t>(c - 'a' + 10);
            else if (hex && c >= 'A' && c <= 'F') d = static_cast<std::uint64_t>(c - 'A' + 10);
            else return std::nullopt;
            if (v > (std::numeric_limits<std::uint64_t>::max() - d) / base) return std::nullopt;
            v = v * base + d;
        }
        return ScalarIdentifier(v);
    }

    constexpr auto operator<=>(const ScalarIdentifier&) const = default;
    constexpr bool operator==(const ScalarIdentifier&) const = default;

    friend std::ostream& operator<<(std::ostream& os, const ScalarIdentifier& id) {
        os << id.to_string();
        return os;
    }
};

// ---------------------------------------------------------------------------
// User-facing identity aliases.  Every identity is a distinct strong type.
// ---------------------------------------------------------------------------
struct StateIdTag       { static constexpr const char* name() noexcept { return "StateId"; } };
struct ProvenanceIdTag  { static constexpr const char* name() noexcept { return "ProvenanceId"; } };
struct ProducerIdTag    { static constexpr const char* name() noexcept { return "ProducerId"; } };
struct ExecutionIdTag   { static constexpr const char* name() noexcept { return "ExecutionId"; } };
struct AttemptIdTag     { static constexpr const char* name() noexcept { return "AttemptId"; } };
struct WorkerIdTag      { static constexpr const char* name() noexcept { return "WorkerId"; } };
struct WorkerBootIdTag  { static constexpr const char* name() noexcept { return "WorkerBootId"; } };
struct DependencyIdTag  { static constexpr const char* name() noexcept { return "DependencyId"; } };
struct ArtifactIdTag    { static constexpr const char* name() noexcept { return "ArtifactId"; } };
struct ModelIdTag       { static constexpr const char* name() noexcept { return "ModelId"; } };
struct AdapterIdTag     { static constexpr const char* name() noexcept { return "AdapterId"; } };
struct CompositionIdTag { static constexpr const char* name() noexcept { return "CompositionId"; } };
struct PolicyIdTag      { static constexpr const char* name() noexcept { return "PolicyId"; } };
struct RuntimeIdTag     { static constexpr const char* name() noexcept { return "RuntimeId"; } };
struct DeviceIdTag      { static constexpr const char* name() noexcept { return "DeviceId"; } };
struct BackendIdTag     { static constexpr const char* name() noexcept { return "BackendId"; } };
struct ToolchainIdTag   { static constexpr const char* name() noexcept { return "ToolchainId"; } };

using StateId       = ScalarIdentifier<StateIdTag>;
using ProvenanceId  = ScalarIdentifier<ProvenanceIdTag>;
using ProducerId    = ScalarIdentifier<ProducerIdTag>;
using ExecutionId   = ScalarIdentifier<ExecutionIdTag>;
using AttemptId     = ScalarIdentifier<AttemptIdTag>;
using WorkerId      = ScalarIdentifier<WorkerIdTag>;
using WorkerBootId  = ScalarIdentifier<WorkerBootIdTag>;
using DependencyId  = ScalarIdentifier<DependencyIdTag>;
using ArtifactId    = ScalarIdentifier<ArtifactIdTag>;
using ModelId       = ScalarIdentifier<ModelIdTag>;
using AdapterId     = ScalarIdentifier<AdapterIdTag>;
using CompositionId = ScalarIdentifier<CompositionIdTag>;
using PolicyId      = ScalarIdentifier<PolicyIdTag>;
using RuntimeId     = ScalarIdentifier<RuntimeIdTag>;
using DeviceId      = ScalarIdentifier<DeviceIdTag>;
using BackendId     = ScalarIdentifier<BackendIdTag>;
using ToolchainId   = ScalarIdentifier<ToolchainIdTag>;

// ---------------------------------------------------------------------------
// Independently rolled generations.  Each domain rolls on its own authority.
// ---------------------------------------------------------------------------
struct ProvenanceGenerationTag { static constexpr const char* name() noexcept { return "ProvenanceGeneration"; } };
struct AttemptGenerationTag    { static constexpr const char* name() noexcept { return "AttemptGeneration"; } };
struct StateGenerationTag     { static constexpr const char* name() noexcept { return "StateGeneration"; } };
struct ProducerGenerationTag  { static constexpr const char* name() noexcept { return "ProducerGeneration"; } };
struct DependencyGenerationTag{ static constexpr const char* name() noexcept { return "DependencyGeneration"; } };
struct PolicyGenerationTag    { static constexpr const char* name() noexcept { return "PolicyGeneration"; } };
struct RuntimeGenerationTag   { static constexpr const char* name() noexcept { return "RuntimeGeneration"; } };
struct ArtifactGenerationTag  { static constexpr const char* name() noexcept { return "ArtifactGeneration"; } };
struct ModelGenerationTag     { static constexpr const char* name() noexcept { return "ModelGeneration"; } };
struct AdapterGenerationTag   { static constexpr const char* name() noexcept { return "AdapterGeneration"; } };
struct CompositionGenerationTag{ static constexpr const char* name() noexcept { return "CompositionGeneration"; } };

using ProvenanceGeneration = ScalarIdentifier<ProvenanceGenerationTag>;
using AttemptGeneration   = ScalarIdentifier<AttemptGenerationTag>;
using StateGeneration      = ScalarIdentifier<StateGenerationTag>;
using ProducerGeneration   = ScalarIdentifier<ProducerGenerationTag>;
using DependencyGeneration = ScalarIdentifier<DependencyGenerationTag>;
using PolicyGeneration     = ScalarIdentifier<PolicyGenerationTag>;
using RuntimeGeneration    = ScalarIdentifier<RuntimeGenerationTag>;
using ArtifactGeneration   = ScalarIdentifier<ArtifactGenerationTag>;
using ModelGeneration      = ScalarIdentifier<ModelGenerationTag>;
using AdapterGeneration    = ScalarIdentifier<AdapterGenerationTag>;
using CompositionGeneration= ScalarIdentifier<CompositionGenerationTag>;

// Domain name lookup for any identity or generation.
template <typename Tag>
constexpr const char* name_of(const ScalarIdentifier<Tag>&) noexcept {
    if constexpr (requires { Tag::name(); }) return Tag::name();
    else return "ScalarIdentifier";
}

} // namespace stateprovenance

namespace std {
template <typename Tag>
struct hash<stateprovenance::ScalarIdentifier<Tag>> {
    std::size_t operator()(const stateprovenance::ScalarIdentifier<Tag>& id) const noexcept {
        std::uint64_t x = id.value + 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        x = x ^ (x >> 31);
        return static_cast<std::size_t>(x);
    }
};
}
