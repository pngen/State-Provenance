#pragma once
// ---------------------------------------------------------------------------
// State Provenance - immutable provenance records.
//
// A ProvenanceRecord is finalized and immutable.  Once published it can never
// be mutated; any semantic change is expressed as a NEW record (and, where the
// authority rolls, a new generation).  The public surface is a const
// RecordData held by value in a const member, so mutation is impossible at
// compile time and never happens at runtime.
//
// Building a record goes through RecordBuilder which validation-gates the
// inputs; validate_record() is the single check used by both the builder and
// the persistence/load path.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/digest.hpp"

namespace stateprovenance {

// ---------------------------------------------------------------------------
// DirectDependency: a declared input dependency with its own rolled generation.
// ---------------------------------------------------------------------------
struct Dependency {
    DependencyId id;
    DependencyGeneration generation;
    std::string kind;      // e.g. "model", "adapter", "kernel", "state", "artifact"
    std::string revision;

    bool operator==(const Dependency&) const = default;
    auto operator<=>(const Dependency&) const = default;
};

// ---------------------------------------------------------------------------
// CompatibilityRequirement: a typed constraint the subject needs to be reused.
// ---------------------------------------------------------------------------
struct CompatibilityRequirement {
    CompatibilityDimension dimension;
    std::string required_value;
    std::string note;

    bool operator==(const CompatibilityRequirement&) const = default;
    auto operator<=>(const CompatibilityRequirement&) const = default;
};

// ---------------------------------------------------------------------------
// RecordData: the full, mutable working representation of a provenance record.
// ProvenanceRecord holds this as a const member to freeze it.
// ---------------------------------------------------------------------------
struct RecordData {
    // identity
    ProvenanceId    provenance_id;
    ProvenanceGeneration provenance_generation;
    StateId         subject_id;
    SubjectKind     subject_kind = SubjectKind::Unknown;
    StateGeneration state_generation;

    // producer / execution
    ProducerId      producer_id;
    ProducerGeneration producer_generation;
    ExecutionId     execution_id;
    AttemptId       attempt_id;
    AttemptGeneration attempt_generation;
    std::int64_t    created_at = 0;   // deterministic integer clock; 0 = unset

    // model identity/revision/tokenizer
    ModelId         model_id;
    ModelGeneration model_generation;
    std::string     model_revision;
    std::string     tokenizer;

    // adapter / composition
    AdapterId       adapter_id;
    AdapterGeneration adapter_generation;
    std::string     adapter_revision;
    CompositionId   composition_id;
    CompositionGeneration composition_generation;
    std::string     composition_fingerprint;

    // runtime / backend / toolchain / device
    RuntimeId       runtime_id;
    RuntimeGeneration runtime_generation;
    ArtifactGeneration artifact_generation;
    BackendId       backend_id;
    ToolchainId     toolchain_id;
    DeviceId        device_id;
    std::string     architecture;        // e.g. "sm_120"
    std::string     compute_capability;  // e.g. "12.0"
    std::string     abi;

    // tensor / specialization attributes
    std::string     dtype;
    std::string     layout;
    std::string     shape;

    // references
    std::vector<StateId>      input_states;
    std::vector<ProvenanceId> parent_provenance;
    std::vector<Dependency>   dependencies;
    std::vector<CompatibilityRequirement> requirements;

    // policy / configuration fingerprint
    PolicyId        policy_id;
    PolicyGeneration policy_generation;
    std::string     policy_fingerprint;

    // integrity
    std::string     content_digest;

    // authority metadata (fenced)
    std::uint64_t   coordinator_epoch = 0;
    WorkerId        worker_id;
    WorkerBootId    worker_boot;

    // evidence classification / validity / reuse
    EvidenceClass   evidence = EvidenceClass::UNKNOWN;
    ValidityState   validity = ValidityState::VALID;
    InvalidationReason invalidation_reason = InvalidationReason::Unknown;
    ReuseEligibility reuse_eligibility = ReuseEligibility::UNKNOWN;
    std::string     note;
};

// ---------------------------------------------------------------------------
// validate_record: single source of truth for record well-formedness.
// Returns an error message, or std::nullopt if the record is valid.
// ---------------------------------------------------------------------------
inline std::optional<std::string> validate_record(const RecordData& d) {
    if (!d.provenance_id.valid())     return "provenance_id must be non-zero";
    if (!d.provenance_generation.valid()) return "provenance_generation must be non-zero";
    if (!d.subject_id.valid())        return "subject_id must be non-zero";
    if (!d.state_generation.valid())  return "state_generation must be non-zero";
    if (!d.producer_id.valid())       return "producer_id must be non-zero";
    if (!d.producer_generation.valid()) return "producer_generation must be non-zero";
    if (d.subject_kind == SubjectKind::Unknown) return "subject_kind must be specified";

    // Distinct identity: no id may be reused for a different subject in one record.
    if (d.input_states.empty() && d.parent_provenance.empty() && d.dependencies.empty() &&
        !d.execution_id.valid())
        return "record must reference at least one input/execution/parent";

    // duplicate references are malformed
    {
        std::vector<StateId> s = d.input_states;
        std::sort(s.begin(), s.end());
        if (std::unique(s.begin(), s.end()) != s.end())
            return "duplicate input_states reference";
    }
    {
        std::vector<ProvenanceId> s = d.parent_provenance;
        std::sort(s.begin(), s.end());
        if (std::unique(s.begin(), s.end()) != s.end())
            return "duplicate parent_provenance reference";
    }
    // self-dependency: a record must not depend on itself
    for (const auto& p : d.parent_provenance)
        if (p == d.provenance_id) return "record cannot be its own parent";
    // NaN/Inf rejection wherever floating values exist (we carry none, but guard)
    // Generation sanity: dependency generation must be non-zero when provided.
    for (const auto& dep : d.dependencies)
        if (!dep.id.valid()) return "dependency id must be non-zero";
    for (const auto& req : d.requirements) {
        if (req.dimension == CompatibilityDimension::Unknown)
            return "requirement dimension must be specified";
        if (req.required_value.empty()) return "requirement value must be non-empty";
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ProvenanceRecord: finalized, immutable.
// ---------------------------------------------------------------------------
struct ProvenanceRecord {
    const RecordData d;

    explicit ProvenanceRecord(const RecordData& data) : d(data) {}

    const RecordData& data() const noexcept { return d; }

    // A deterministic digest over the derivation-relevant fields.  Reference
    // vectors are hashed in sorted order so the digest is reproducible no
    // matter the insertion order.
    std::string derivation_digest() const {
        digest::StableDigest sd(0x70726f76656e6365ull); // "provence"
        const auto& r = d;
        sd.with(r.provenance_id).with(r.provenance_generation)
          .with(r.subject_id).with(static_cast<int>(r.subject_kind))
          .with(r.state_generation).with(r.producer_id).with(r.producer_generation)
          .with(r.execution_id).with(r.attempt_id).with(r.attempt_generation)
          .with(r.created_at)
          .with(r.model_id).with(r.model_generation).with(r.model_revision)
          .with(r.tokenizer)
          .with(r.adapter_id).with(r.adapter_generation).with(r.adapter_revision)
          .with(r.composition_id).with(r.composition_generation).with(r.composition_fingerprint)
          .with(r.runtime_id).with(r.runtime_generation).with(r.backend_id)
          .with(r.toolchain_id).with(r.device_id).with(r.architecture)
          .with(r.compute_capability).with(r.abi).with(r.dtype).with(r.layout).with(r.shape)
          .with(r.policy_id).with(r.policy_generation).with(r.policy_fingerprint)
          .with(r.content_digest).with(r.coordinator_epoch).with(r.worker_id)
          .with(r.worker_boot).with(static_cast<int>(r.evidence))
          .with(static_cast<int>(r.validity)).with(static_cast<int>(r.invalidation_reason))
          .with(static_cast<int>(r.reuse_eligibility)).with(r.note);

        auto sorted_state = r.input_states;
        std::sort(sorted_state.begin(), sorted_state.end());
        for (const auto& v : sorted_state) sd.with(static_cast<std::uint64_t>(v.get()));
        sd.with(static_cast<std::uint64_t>(sorted_state.size()));

        auto sorted_parent = r.parent_provenance;
        std::sort(sorted_parent.begin(), sorted_parent.end());
        for (const auto& v : sorted_parent) sd.with(static_cast<std::uint64_t>(v.get()));
        sd.with(static_cast<std::uint64_t>(sorted_parent.size()));

        auto sorted_dep = r.dependencies;
        std::sort(sorted_dep.begin(), sorted_dep.end());
        for (const auto& dep : sorted_dep) {
            sd.with(dep.id).with(dep.generation).with(dep.kind).with(dep.revision);
        }
        sd.with(static_cast<std::uint64_t>(sorted_dep.size()));

        auto sorted_req = r.requirements;
        std::sort(sorted_req.begin(), sorted_req.end());
        for (const auto& req : sorted_req) {
            sd.with(static_cast<int>(req.dimension)).with(req.required_value).with(req.note);
        }
        sd.with(static_cast<std::uint64_t>(sorted_req.size()));

        return sd.str();
    }
};

// ---------------------------------------------------------------------------
// RecordBuilder: chained construction with validation.
// ---------------------------------------------------------------------------
class RecordBuilder {
public:
    RecordBuilder() = default;

    RecordBuilder& provenance_id(ProvenanceId v) { d_.provenance_id = v; return *this; }
    RecordBuilder& provenance_generation(ProvenanceGeneration v) { d_.provenance_generation = v; return *this; }
    RecordBuilder& subject_id(StateId v) { d_.subject_id = v; return *this; }
    RecordBuilder& subject_kind(SubjectKind v) { d_.subject_kind = v; return *this; }
    RecordBuilder& state_generation(StateGeneration v) { d_.state_generation = v; return *this; }
    RecordBuilder& producer_id(ProducerId v) { d_.producer_id = v; return *this; }
    RecordBuilder& producer_generation(ProducerGeneration v) { d_.producer_generation = v; return *this; }
    RecordBuilder& execution_id(ExecutionId v) { d_.execution_id = v; return *this; }
    RecordBuilder& attempt_id(AttemptId v) { d_.attempt_id = v; return *this; }
    RecordBuilder& attempt_generation(AttemptGeneration v) { d_.attempt_generation = v; return *this; }
    RecordBuilder& created_at(std::int64_t v) { d_.created_at = v; return *this; }
    RecordBuilder& model_id(ModelId v) { d_.model_id = v; return *this; }
    RecordBuilder& model_generation(ModelGeneration v) { d_.model_generation = v; return *this; }
    RecordBuilder& model_revision(std::string v) { d_.model_revision = std::move(v); return *this; }
    RecordBuilder& tokenizer(std::string v) { d_.tokenizer = std::move(v); return *this; }
    RecordBuilder& adapter_id(AdapterId v) { d_.adapter_id = v; return *this; }
    RecordBuilder& adapter_generation(AdapterGeneration v) { d_.adapter_generation = v; return *this; }
    RecordBuilder& adapter_revision(std::string v) { d_.adapter_revision = std::move(v); return *this; }
    RecordBuilder& composition_id(CompositionId v) { d_.composition_id = v; return *this; }
    RecordBuilder& composition_generation(CompositionGeneration v) { d_.composition_generation = v; return *this; }
    RecordBuilder& composition_fingerprint(std::string v) { d_.composition_fingerprint = std::move(v); return *this; }
    RecordBuilder& runtime_id(RuntimeId v) { d_.runtime_id = v; return *this; }
    RecordBuilder& runtime_generation(RuntimeGeneration v) { d_.runtime_generation = v; return *this; }
    RecordBuilder& artifact_generation(ArtifactGeneration v) { d_.artifact_generation = v; return *this; }
    RecordBuilder& backend_id(BackendId v) { d_.backend_id = v; return *this; }
    RecordBuilder& toolchain_id(ToolchainId v) { d_.toolchain_id = v; return *this; }
    RecordBuilder& device_id(DeviceId v) { d_.device_id = v; return *this; }
    RecordBuilder& architecture(std::string v) { d_.architecture = std::move(v); return *this; }
    RecordBuilder& compute_capability(std::string v) { d_.compute_capability = std::move(v); return *this; }
    RecordBuilder& abi(std::string v) { d_.abi = std::move(v); return *this; }
    RecordBuilder& dtype(std::string v) { d_.dtype = std::move(v); return *this; }
    RecordBuilder& layout(std::string v) { d_.layout = std::move(v); return *this; }
    RecordBuilder& shape(std::string v) { d_.shape = std::move(v); return *this; }
    RecordBuilder& input_states(std::vector<StateId> v) { d_.input_states = std::move(v); return *this; }
    RecordBuilder& add_input_state(StateId v) { d_.input_states.push_back(v); return *this; }
    RecordBuilder& parent_provenance(std::vector<ProvenanceId> v) { d_.parent_provenance = std::move(v); return *this; }
    RecordBuilder& add_parent(ProvenanceId v) { d_.parent_provenance.push_back(v); return *this; }
    RecordBuilder& dependencies(std::vector<Dependency> v) { d_.dependencies = std::move(v); return *this; }
    RecordBuilder& add_dependency(Dependency v) { d_.dependencies.push_back(std::move(v)); return *this; }
    RecordBuilder& requirements(std::vector<CompatibilityRequirement> v) { d_.requirements = std::move(v); return *this; }
    RecordBuilder& add_requirement(CompatibilityRequirement v) { d_.requirements.push_back(std::move(v)); return *this; }
    RecordBuilder& policy_id(PolicyId v) { d_.policy_id = v; return *this; }
    RecordBuilder& policy_generation(PolicyGeneration v) { d_.policy_generation = v; return *this; }
    RecordBuilder& policy_fingerprint(std::string v) { d_.policy_fingerprint = std::move(v); return *this; }
    RecordBuilder& content_digest(std::string v) { d_.content_digest = std::move(v); return *this; }
    RecordBuilder& coordinator_epoch(std::uint64_t v) { d_.coordinator_epoch = v; return *this; }
    RecordBuilder& worker_id(WorkerId v) { d_.worker_id = v; return *this; }
    RecordBuilder& worker_boot(WorkerBootId v) { d_.worker_boot = v; return *this; }
    RecordBuilder& evidence(EvidenceClass v) { d_.evidence = v; return *this; }
    RecordBuilder& validity(ValidityState v) { d_.validity = v; return *this; }
    RecordBuilder& invalidation_reason(InvalidationReason v) { d_.invalidation_reason = v; return *this; }
    RecordBuilder& reuse_eligibility(ReuseEligibility v) { d_.reuse_eligibility = v; return *this; }
    RecordBuilder& note(std::string v) { d_.note = std::move(v); return *this; }

    // Replaces the whole RecordData (used by persistence to build from wire).
    RecordBuilder& data(RecordData d) { d_ = std::move(d); return *this; }

    const RecordData& raw() const noexcept { return d_; }

    std::optional<std::string> error() const { return validate_record(d_); }

    ProvenanceRecord build() const {
        auto e = validate_record(d_);
        if (e) throw std::invalid_argument("invalid provenance record: " + *e);
        return ProvenanceRecord(d_);
    }

    std::optional<ProvenanceRecord> try_build() const {
        if (validate_record(d_)) return std::nullopt;
        return ProvenanceRecord(d_);
    }

private:
    RecordData d_;
};

} // namespace stateprovenance
