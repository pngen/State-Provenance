#pragma once
// ---------------------------------------------------------------------------
// State Provenance - typed compatibility and reuse-eligibility evaluation.
//
// A ReuseRequest describes a requested execution environment.  Evaluation is
// explicit and explainable: it returns a typed ReuseEligibility plus the exact
// failed dimension(s) and per-dimension reasons.  Every denial is precise.
// Empty request fields mean "not constraining".  A non-empty request value
// against a missing record value is INCOMPLETE_PROVENANCE.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/record.hpp"

namespace stateprovenance {

struct ReuseRequest {
    ModelId       model_id;
    ModelGeneration model_generation;
    std::string   model_revision;
    std::string   tokenizer;

    AdapterId     adapter_id;
    AdapterGeneration adapter_generation;
    std::string   adapter_revision;
    CompositionId composition_id;
    CompositionGeneration composition_generation;
    std::string   composition_fingerprint;

    RuntimeId     runtime_id;
    RuntimeGeneration runtime_generation;
    BackendId     backend_id;
    ToolchainId   toolchain_id;

    std::string   architecture;
    std::string   compute_capability;
    std::string   abi;

    std::string   dtype;
    std::string   layout;
    std::string   shape;

    PolicyGeneration policy_generation;
    ArtifactGeneration artifact_generation;
    DependencyGeneration dependency_generation;
};

struct ReuseDecision {
    ReuseEligibility eligibility = ReuseEligibility::UNKNOWN;
    std::vector<CompatibilityDimension> failed_dimensions;
    std::vector<std::string> reasons;

    std::string summary() const {
        std::string s = name_of(eligibility);
        if (!reasons.empty()) {
            s += " (";
            for (std::size_t i = 0; i < reasons.size(); ++i) {
                if (i) s += "; ";
                s += reasons[i];
            }
            s += ")";
        }
        return s;
    }
};

inline ReuseDecision evaluate_reuse(const ProvenanceRecord& rec, const ReuseRequest& req) {
    const auto& r = rec.data();
    ReuseDecision d;
    d.eligibility = ReuseEligibility::ELIGIBLE;

    bool any_mismatch = false;
    bool any_incomplete = false;
    std::vector<CompatibilityDimension> mismatch_dims, incomplete_dims;
    std::vector<std::string> reasons;

    auto dim_check = [&](CompatibilityDimension dim,
                         bool req_set, bool rec_set, bool match,
                         const std::string& what) {
        if (!req_set) return;
        if (!rec_set) { any_incomplete = true; incomplete_dims.push_back(dim);
            reasons.push_back(what + " required by request but absent in provenance"); return; }
        if (!match) { any_mismatch = true; mismatch_dims.push_back(dim);
            reasons.push_back(what + " incompatible with requested environment"); return; }
    };

    auto id_req_set = [](auto id) { return id.valid(); };

    dim_check(CompatibilityDimension::ModelIdentity,
              id_req_set(req.model_id), id_req_set(r.model_id),
              req.model_id == r.model_id, "model identity");
    dim_check(CompatibilityDimension::ModelRevision,
              !req.model_revision.empty(), !r.model_revision.empty(),
              req.model_revision == r.model_revision, "model revision");
    dim_check(CompatibilityDimension::Tokenizer,
              !req.tokenizer.empty(), !r.tokenizer.empty(),
              req.tokenizer == r.tokenizer, "tokenizer/vocabulary");
    dim_check(CompatibilityDimension::AdapterIdentity,
              id_req_set(req.adapter_id), id_req_set(r.adapter_id),
              req.adapter_id == r.adapter_id, "adapter identity");
    dim_check(CompatibilityDimension::AdapterRevision,
              !req.adapter_revision.empty(), !r.adapter_revision.empty(),
              req.adapter_revision == r.adapter_revision, "adapter revision");
    dim_check(CompatibilityDimension::AdapterComposition,
              !req.composition_fingerprint.empty(), !r.composition_fingerprint.empty(),
              req.composition_fingerprint == r.composition_fingerprint, "adapter composition");
    dim_check(CompatibilityDimension::Backend,
              id_req_set(req.backend_id), id_req_set(r.backend_id),
              req.backend_id == r.backend_id, "backend");
    dim_check(CompatibilityDimension::Runtime,
              id_req_set(req.runtime_id), id_req_set(r.runtime_id),
              req.runtime_id == r.runtime_id, "runtime");
    dim_check(CompatibilityDimension::Toolchain,
              id_req_set(req.toolchain_id), id_req_set(r.toolchain_id),
              req.toolchain_id == r.toolchain_id, "toolchain");
    dim_check(CompatibilityDimension::Architecture,
              !req.architecture.empty(), !r.architecture.empty(),
              req.architecture == r.architecture, "architecture");
    dim_check(CompatibilityDimension::ComputeCapability,
              !req.compute_capability.empty(), !r.compute_capability.empty(),
              req.compute_capability == r.compute_capability, "compute capability");
    dim_check(CompatibilityDimension::ABI,
              !req.abi.empty(), !r.abi.empty(),
              req.abi == r.abi, "ABI");
    dim_check(CompatibilityDimension::Dtype,
              !req.dtype.empty(), !r.dtype.empty(),
              req.dtype == r.dtype, "dtype");
    dim_check(CompatibilityDimension::Layout,
              !req.layout.empty(), !r.layout.empty(),
              req.layout == r.layout, "layout");
    dim_check(CompatibilityDimension::TensorGeometry,
              !req.shape.empty(), !r.shape.empty(),
              req.shape == r.shape, "tensor geometry");

    dim_check(CompatibilityDimension::ModelGen,
              id_req_set(req.model_generation), id_req_set(r.model_generation),
              req.model_generation == r.model_generation, "model generation");
    dim_check(CompatibilityDimension::AdapterGen,
              id_req_set(req.adapter_generation), id_req_set(r.adapter_generation),
              req.adapter_generation == r.adapter_generation, "adapter generation");
    dim_check(CompatibilityDimension::CompositionGen,
              id_req_set(req.composition_generation), id_req_set(r.composition_generation),
              req.composition_generation == r.composition_generation, "composition generation");
    dim_check(CompatibilityDimension::RuntimeGen,
              id_req_set(req.runtime_generation), id_req_set(r.runtime_generation),
              req.runtime_generation == r.runtime_generation, "runtime generation");
    dim_check(CompatibilityDimension::PolicyGen,
              id_req_set(req.policy_generation), id_req_set(r.policy_generation),
              req.policy_generation == r.policy_generation, "policy generation");
    dim_check(CompatibilityDimension::ArtifactGen,
              id_req_set(req.artifact_generation), id_req_set(r.artifact_generation),
              req.artifact_generation == r.artifact_generation, "artifact generation");

    // Deterministic outcome precedence.
    if (r.validity == ValidityState::INVALIDATED) {
        d.eligibility = ReuseEligibility::INVALIDATED;
        if (reasons.empty()) reasons.push_back("subject has been invalidated");
        d.failed_dimensions.push_back(CompatibilityDimension::ProvenanceIntegrity);
    } else if (r.validity == ValidityState::STALE) {
        d.eligibility = ReuseEligibility::STALE;
        if (reasons.empty()) reasons.push_back("subject is stale");
    } else if (any_mismatch) {
        d.eligibility = ReuseEligibility::INCOMPATIBLE;
        d.failed_dimensions = std::move(mismatch_dims);
    } else if (any_incomplete) {
        d.eligibility = ReuseEligibility::INCOMPLETE_PROVENANCE;
        d.failed_dimensions = std::move(incomplete_dims);
    } else if (r.validity == ValidityState::UNKNOWN) {
        d.eligibility = ReuseEligibility::UNKNOWN;
    } else {
        d.eligibility = ReuseEligibility::ELIGIBLE;
    }

    d.reasons = std::move(reasons);
    return d;
}

} // namespace stateprovenance
