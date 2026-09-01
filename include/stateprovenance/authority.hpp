#pragma once
// ---------------------------------------------------------------------------
// State Provenance - distributed mutation authority and fencing.
//
// Every mutation claim carries a full set of authority values.  The AuthorityState
// holds the values currently considered current.  A claim is accepted only if
// EVERY fenced value matches the current authority; otherwise it is rejected
// deterministically, with the exact failing dimension reported.
//
// Old work can never become current after:
//   coordinator epoch rollover, worker restart (boot change), attempt retry,
//   provenance/state generation rollover, model/adapter/composition revision,
//   dependency/policy/artifact/runtime generation changes.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "stateprovenance/ids.hpp"

namespace stateprovenance {

enum class FenceOutcome : std::uint8_t { ACCEPT, REJECT };

struct FenceVerdict {
    FenceOutcome outcome = FenceOutcome::REJECT;
    std::string dimension;   // failing fence dimension (empty on accept)
    std::string detail;      // human-readable explanation

    bool accepted() const noexcept { return outcome == FenceOutcome::ACCEPT; }
};

// The authority values a mutation asserts. Missing (null) optional fence
// dimensions are treated as "not constrained" and pass the check.
struct AuthorityClaim {
    WorkerId      worker_id;
    WorkerBootId  worker_boot;
    AttemptId     attempt_id;
    AttemptGeneration attempt_generation;
    StateGeneration    state_generation;
    ProvenanceGeneration provenance_generation;
    std::uint64_t coordinator_epoch = 0;

    // Optional per-domain generation constraints.
    std::optional<ArtifactGeneration>    artifact_generation;
    std::optional<ModelGeneration>       model_generation;
    std::optional<AdapterGeneration>     adapter_generation;
    std::optional<CompositionGeneration> composition_generation;
    std::optional<PolicyGeneration>      policy_generation;
    std::optional<DependencyGeneration>  dependency_generation;
};

class AuthorityState {
public:
    AuthorityState() { epoch_ = 1; }

    // ---- mutation of authoritative values ----
    std::uint64_t roll_epoch() { return ++epoch_; }
    std::uint64_t epoch() const { return epoch_; }
    void set_epoch(std::uint64_t e) { epoch_ = (e == 0) ? 1 : e; }

    void register_boot(WorkerId worker, WorkerBootId boot) { boot_[worker] = boot; }
    std::optional<WorkerBootId> boot_of(WorkerId worker) const {
        auto it = boot_.find(worker);
        if (it == boot_.end()) return std::nullopt;
        return it->second;
    }

    void begin_attempt(AttemptId id, AttemptGeneration gen) {
        attempt_id_ = id;
        attempt_generation_ = gen;
    }
    AttemptId attempt_id() const { return attempt_id_; }
    AttemptGeneration attempt_generation() const { return attempt_generation_; }

    void roll_state_generation(StateGeneration g) { state_generation_ = g; }
    StateGeneration state_generation() const { return state_generation_; }

    void roll_provenance_generation(ProvenanceGeneration g) { provenance_generation_ = g; }
    ProvenanceGeneration provenance_generation() const { return provenance_generation_; }

    void set_artifact_generation(ArtifactGeneration g) { artifact_generation_ = g; }
    void set_model_generation(ModelGeneration g) { model_generation_ = g; }
    void set_adapter_generation(AdapterGeneration g) { adapter_generation_ = g; }
    void set_composition_generation(CompositionGeneration g) { composition_generation_ = g; }
    void set_policy_generation(PolicyGeneration g) { policy_generation_ = g; }
    void set_dependency_generation(DependencyGeneration g) { dependency_generation_ = g; }

    ArtifactGeneration artifact_generation() const { return artifact_generation_; }
    ModelGeneration model_generation() const { return model_generation_; }
    AdapterGeneration adapter_generation() const { return adapter_generation_; }
    CompositionGeneration composition_generation() const { return composition_generation_; }
    PolicyGeneration policy_generation() const { return policy_generation_; }
    DependencyGeneration dependency_generation() const { return dependency_generation_; }

    // ---- fencing ----
    FenceVerdict evaluate_claim(const AuthorityClaim& claim) const {
        if (claim.coordinator_epoch != epoch_)
            return reject("CoordinatorEpoch",
                          "claim epoch " + std::to_string(claim.coordinator_epoch) +
                          " != current epoch " + std::to_string(epoch_));
        if (claim.worker_id.valid()) {
            auto cur = boot_of(claim.worker_id);
            if (!cur)
                return reject("WorkerBootId",
                              "worker " + claim.worker_id.to_string() + " not registered");
            if (cur.value() != claim.worker_boot)
                return reject("WorkerBootId",
                              "worker " + claim.worker_id.to_string() +
                              " boot " + claim.worker_boot.to_string() +
                              " stale (current " + cur.value().to_string() + ")");
        }
        if (claim.attempt_id.valid() && attempt_id_.valid() &&
            claim.attempt_id != attempt_id_)
            return reject("AttemptId",
                          "attempt " + claim.attempt_id.to_string() +
                          " stale (current " + attempt_id_.to_string() + ")");
        if (claim.attempt_generation.valid() && attempt_generation_.valid() &&
            claim.attempt_generation != attempt_generation_)
            return reject("AttemptGeneration",
                          "attempt generation stale");
        if (claim.state_generation.valid() && state_generation_.valid() &&
            claim.state_generation != state_generation_)
            return reject("StateGeneration",
                          "state generation stale");
        if (claim.provenance_generation.valid() && provenance_generation_.valid() &&
            claim.provenance_generation != provenance_generation_)
            return reject("ProvenanceGeneration",
                          "provenance generation stale");

        if (claim.artifact_generation && artifact_generation_.valid() &&
            claim.artifact_generation.value() != artifact_generation_)
            return reject("ArtifactGeneration", "artifact generation stale");
        if (claim.model_generation && model_generation_.valid() &&
            claim.model_generation.value() != model_generation_)
            return reject("ModelGeneration", "model generation stale");
        if (claim.adapter_generation && adapter_generation_.valid() &&
            claim.adapter_generation.value() != adapter_generation_)
            return reject("AdapterGeneration", "adapter generation stale");
        if (claim.composition_generation && composition_generation_.valid() &&
            claim.composition_generation.value() != composition_generation_)
            return reject("CompositionGeneration", "composition generation stale");
        if (claim.policy_generation && policy_generation_.valid() &&
            claim.policy_generation.value() != policy_generation_)
            return reject("PolicyGeneration", "policy generation stale");
        if (claim.dependency_generation && dependency_generation_.valid() &&
            claim.dependency_generation.value() != dependency_generation_)
            return reject("DependencyGeneration", "dependency generation stale");

        return FenceVerdict{FenceOutcome::ACCEPT, "", "accepted"};
    }

private:
    static FenceVerdict reject(std::string dim, std::string detail) {
        return FenceVerdict{FenceOutcome::REJECT, std::move(dim), std::move(detail)};
    }

    std::uint64_t epoch_ = 1;
    std::map<WorkerId, WorkerBootId> boot_;
    AttemptId attempt_id_;
    AttemptGeneration attempt_generation_;
    StateGeneration state_generation_;
    ProvenanceGeneration provenance_generation_;
    ArtifactGeneration artifact_generation_;
    ModelGeneration model_generation_;
    AdapterGeneration adapter_generation_;
    CompositionGeneration composition_generation_;
    PolicyGeneration policy_generation_;
    DependencyGeneration dependency_generation_;
};

} // namespace stateprovenance
