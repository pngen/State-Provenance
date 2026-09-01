#pragma once
// ---------------------------------------------------------------------------
// State Provenance - typed domain vocabulary.
// Every enumerated domain is a distinct enum class; values are never silently
// promoted, and unknown values are rejected at the persistence boundary.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace stateprovenance {

template <typename E>
struct EnumEntry { E value; const char* name; };

template <typename E, std::size_t N>
constexpr const char* enum_name(E v, const std::array<EnumEntry<E>, N>& tbl) noexcept {
    for (const auto& e : tbl) if (e.value == v) return e.name;
    return "Unknown";
}

template <typename E, std::size_t N>
std::optional<E> enum_parse(std::string_view s, const std::array<EnumEntry<E>, N>& tbl) noexcept {
    for (const auto& e : tbl) if (s == e.name) return e.value;
    return std::nullopt;
}

template <typename E, std::size_t N>
constexpr bool enum_is_valid(E v, const std::array<EnumEntry<E>, N>& tbl) noexcept {
    for (const auto& e : tbl) if (e.value == v) return true;
    return false;
}


#define SP_ENUM_TABLE(Type, ...)     inline constexpr std::array<EnumEntry<Type>, sizeof((Type[]){__VA_ARGS__}) / sizeof(Type)> name_table_##Type =         std::array<EnumEntry<Type>, sizeof((Type[]){__VA_ARGS__}) / sizeof(Type)>{{ {Type::__VA_ARGS__, #__VA_ARGS__} }}

// ---------------------------------------------------------------------------
// SubjectKind: what kind of machine-produced object the provenance subject is.
// ---------------------------------------------------------------------------
enum class SubjectKind : std::uint8_t {
    KVState, TensorState, ReusablePrefix, ModelArtifact, Adapter, CompiledKernel,
    ExecutionGraph, Checkpoint, Buffer, ModelResidency, DerivedExecutionPlan,
    CompiledArtifact, RuntimeCache, ReusableComputation, Generic, Unknown
};
inline constexpr std::array<EnumEntry<SubjectKind>,16> kSubjectKind{{
    {SubjectKind::KVState,"KVState"},{SubjectKind::TensorState,"TensorState"},
    {SubjectKind::ReusablePrefix,"ReusablePrefix"},{SubjectKind::ModelArtifact,"ModelArtifact"},
    {SubjectKind::Adapter,"Adapter"},{SubjectKind::CompiledKernel,"CompiledKernel"},
    {SubjectKind::ExecutionGraph,"ExecutionGraph"},{SubjectKind::Checkpoint,"Checkpoint"},
    {SubjectKind::Buffer,"Buffer"},{SubjectKind::ModelResidency,"ModelResidency"},
    {SubjectKind::DerivedExecutionPlan,"DerivedExecutionPlan"},{SubjectKind::CompiledArtifact,"CompiledArtifact"},
    {SubjectKind::RuntimeCache,"RuntimeCache"},{SubjectKind::ReusableComputation,"ReusableComputation"},
    {SubjectKind::Generic,"Generic"},{SubjectKind::Unknown,"Unknown"}
}};
inline constexpr const char* name_of(SubjectKind v) noexcept { return enum_name(v, kSubjectKind); }
inline std::optional<SubjectKind> parse_SubjectKind(std::string_view s) noexcept { return enum_parse(s, kSubjectKind); }

// ---------------------------------------------------------------------------
// EvidenceClass: source/quality classification of provenance evidence.
// ---------------------------------------------------------------------------
enum class EvidenceClass : std::uint8_t {
    MEASURED, REPORTED, DERIVED, RECONSTRUCTED, ESTIMATED, UNKNOWN
};
inline constexpr std::array<EnumEntry<EvidenceClass>,6> kEvidenceClass{{
    {EvidenceClass::MEASURED,"MEASURED"},{EvidenceClass::REPORTED,"REPORTED"},
    {EvidenceClass::DERIVED,"DERIVED"},{EvidenceClass::RECONSTRUCTED,"RECONSTRUCTED"},
    {EvidenceClass::ESTIMATED,"ESTIMATED"},{EvidenceClass::UNKNOWN,"UNKNOWN"}
}};
inline constexpr const char* name_of(EvidenceClass v) noexcept { return enum_name(v, kEvidenceClass); }
inline std::optional<EvidenceClass> parse_EvidenceClass(std::string_view s) noexcept { return enum_parse(s, kEvidenceClass); }

// ---------------------------------------------------------------------------
// ValidityState: is the subject still valid to treat as current?
// ---------------------------------------------------------------------------
enum class ValidityState : std::uint8_t { VALID, STALE, INVALIDATED, UNKNOWN };
inline constexpr std::array<EnumEntry<ValidityState>,4> kValidityState{{
    {ValidityState::VALID,"VALID"},{ValidityState::STALE,"STALE"},
    {ValidityState::INVALIDATED,"INVALIDATED"},{ValidityState::UNKNOWN,"UNKNOWN"}
}};
inline constexpr const char* name_of(ValidityState v) noexcept { return enum_name(v, kValidityState); }
inline std::optional<ValidityState> parse_ValidityState(std::string_view s) noexcept { return enum_parse(s, kValidityState); }

// ---------------------------------------------------------------------------
// ReuseEligibility: explicit, typed reuse decision.
// ---------------------------------------------------------------------------
enum class ReuseEligibility : std::uint8_t {
    ELIGIBLE, INELIGIBLE, STALE, INVALIDATED, INCOMPATIBLE,
    INCOMPLETE_PROVENANCE, UNKNOWN
};
inline constexpr std::array<EnumEntry<ReuseEligibility>,7> kReuseEligibility{{
    {ReuseEligibility::ELIGIBLE,"ELIGIBLE"},{ReuseEligibility::INELIGIBLE,"INELIGIBLE"},
    {ReuseEligibility::STALE,"STALE"},{ReuseEligibility::INVALIDATED,"INVALIDATED"},
    {ReuseEligibility::INCOMPATIBLE,"INCOMPATIBLE"},
    {ReuseEligibility::INCOMPLETE_PROVENANCE,"INCOMPLETE_PROVENANCE"},
    {ReuseEligibility::UNKNOWN,"UNKNOWN"}
}};
inline constexpr const char* name_of(ReuseEligibility v) noexcept { return enum_name(v, kReuseEligibility); }
inline std::optional<ReuseEligibility> parse_ReuseEligibility(std::string_view s) noexcept { return enum_parse(s, kReuseEligibility); }

// ---------------------------------------------------------------------------
// InvalidationReason: why a subject became stale/invalid.
// ---------------------------------------------------------------------------
enum class InvalidationReason : std::uint8_t {
    ModelRevisionChange, AdapterRevisionChange, CompositionChange,
    ArtifactInvalidation, DependencyInvalidation, RuntimeBackendIncompatibility,
    ArchitectureIncompatibility, PolicyGenerationChange, OperatorInvalidation,
    CorruptState, ProducerGenerationRollover, Unknown
};
inline constexpr std::array<EnumEntry<InvalidationReason>,12> kInvalidationReason{{
    {InvalidationReason::ModelRevisionChange,"ModelRevisionChange"},
    {InvalidationReason::AdapterRevisionChange,"AdapterRevisionChange"},
    {InvalidationReason::CompositionChange,"CompositionChange"},
    {InvalidationReason::ArtifactInvalidation,"ArtifactInvalidation"},
    {InvalidationReason::DependencyInvalidation,"DependencyInvalidation"},
    {InvalidationReason::RuntimeBackendIncompatibility,"RuntimeBackendIncompatibility"},
    {InvalidationReason::ArchitectureIncompatibility,"ArchitectureIncompatibility"},
    {InvalidationReason::PolicyGenerationChange,"PolicyGenerationChange"},
    {InvalidationReason::OperatorInvalidation,"OperatorInvalidation"},
    {InvalidationReason::CorruptState,"CorruptState"},
    {InvalidationReason::ProducerGenerationRollover,"ProducerGenerationRollover"},
    {InvalidationReason::Unknown,"Unknown"}
}};
inline constexpr const char* name_of(InvalidationReason v) noexcept { return enum_name(v, kInvalidationReason); }
inline std::optional<InvalidationReason> parse_InvalidationReason(std::string_view s) noexcept { return enum_parse(s, kInvalidationReason); }

// ---------------------------------------------------------------------------
// CompatibilityDimension: a single typed dimension of compatibility checking.
// ---------------------------------------------------------------------------
enum class CompatibilityDimension : std::uint8_t {
    ModelIdentity, ModelRevision, Tokenizer, AdapterIdentity, AdapterRevision,
    AdapterComposition, Backend, Runtime, Compiler, Toolchain, Architecture,
    ComputeCapability, ABI, Dtype, Layout, TensorGeometry, ShapeSpecialization,
    KernelSpecialization, GraphTopology, ModelGen, AdapterGen, CompositionGen,
    RuntimeGen, PolicyGen, ArtifactGen, ResidencyGen, DependencyGen,
    ProvenanceIntegrity, Unknown
};
inline constexpr std::array<EnumEntry<CompatibilityDimension>,29> kCompatibilityDimension{{
    {CompatibilityDimension::ModelIdentity,"ModelIdentity"},
    {CompatibilityDimension::ModelRevision,"ModelRevision"},
    {CompatibilityDimension::Tokenizer,"Tokenizer"},
    {CompatibilityDimension::AdapterIdentity,"AdapterIdentity"},
    {CompatibilityDimension::AdapterRevision,"AdapterRevision"},
    {CompatibilityDimension::AdapterComposition,"AdapterComposition"},
    {CompatibilityDimension::Backend,"Backend"},
    {CompatibilityDimension::Runtime,"Runtime"},
    {CompatibilityDimension::Compiler,"Compiler"},
    {CompatibilityDimension::Toolchain,"Toolchain"},
    {CompatibilityDimension::Architecture,"Architecture"},
    {CompatibilityDimension::ComputeCapability,"ComputeCapability"},
    {CompatibilityDimension::ABI,"ABI"},
    {CompatibilityDimension::Dtype,"Dtype"},
    {CompatibilityDimension::Layout,"Layout"},
    {CompatibilityDimension::TensorGeometry,"TensorGeometry"},
    {CompatibilityDimension::ShapeSpecialization,"ShapeSpecialization"},
    {CompatibilityDimension::KernelSpecialization,"KernelSpecialization"},
    {CompatibilityDimension::GraphTopology,"GraphTopology"},
    {CompatibilityDimension::ModelGen,"ModelGen"},
    {CompatibilityDimension::AdapterGen,"AdapterGen"},
    {CompatibilityDimension::CompositionGen,"CompositionGen"},
    {CompatibilityDimension::RuntimeGen,"RuntimeGen"},
    {CompatibilityDimension::PolicyGen,"PolicyGen"},
    {CompatibilityDimension::ArtifactGen,"ArtifactGen"},
    {CompatibilityDimension::ResidencyGen,"ResidencyGen"},
    {CompatibilityDimension::DependencyGen,"DependencyGen"},
    {CompatibilityDimension::ProvenanceIntegrity,"ProvenanceIntegrity"},
    {CompatibilityDimension::Unknown,"Unknown"}
}};
inline constexpr const char* name_of(CompatibilityDimension v) noexcept { return enum_name(v, kCompatibilityDimension); }
inline std::optional<CompatibilityDimension> parse_CompatibilityDimension(std::string_view s) noexcept { return enum_parse(s, kCompatibilityDimension); }


// ---------------------------------------------------------------------------
// Enum value validity predicates (defined after all enums are declared).
// ---------------------------------------------------------------------------
inline constexpr bool is_valid(SubjectKind v) noexcept { return enum_is_valid(v, kSubjectKind); }
inline constexpr bool is_valid(EvidenceClass v) noexcept { return enum_is_valid(v, kEvidenceClass); }
inline constexpr bool is_valid(ValidityState v) noexcept { return enum_is_valid(v, kValidityState); }
inline constexpr bool is_valid(ReuseEligibility v) noexcept { return enum_is_valid(v, kReuseEligibility); }
inline constexpr bool is_valid(InvalidationReason v) noexcept { return enum_is_valid(v, kInvalidationReason); }
inline constexpr bool is_valid(CompatibilityDimension v) noexcept { return enum_is_valid(v, kCompatibilityDimension); }

} // namespace stateprovenance
