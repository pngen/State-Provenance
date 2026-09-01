#pragma once
// ---------------------------------------------------------------------------
// State Provenance - thread-safe provenance runtime.
//
// Holds immutable provenance records plus an explicit derivation graph, a
// mutable current-status overlay (validity / invalidation), authoritative
// fencing state, and indexed lookups.  Reads use a shared lock and run in
// parallel; writes are exclusive.  No lock is ever held across I/O: the
// persistence path snapshots under the lock, then performs file I/O outside it.
// Intrusive backends (CUDA, TCP) never run under the store lock.
// ---------------------------------------------------------------------------
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/graph.hpp"
#include "stateprovenance/authority.hpp"
#include "stateprovenance/compat.hpp"
#include "stateprovenance/invalidate.hpp"
#include "stateprovenance/digest.hpp"

namespace stateprovenance {

struct PublishOutcome {
    bool ok = false;
    std::string error;
    std::shared_ptr<const ProvenanceRecord> record;
};

struct InvalidationOutcome {
    bool ok = false;
    std::string error;
    InvalidationResult result;
};

// Current (mutable) status for a subject; separate from the immutable record.
struct RecordStatus {
    ValidityState validity = ValidityState::VALID;
    InvalidationReason reason = InvalidationReason::Unknown;
    ProvenanceGeneration provenance_generation;
    std::string detail;
};

struct AuthoritySnapshot {
    std::uint64_t epoch = 1;
    std::map<WorkerId, WorkerBootId> boots;
    AttemptId attempt_id;
    AttemptGeneration attempt_generation;
    StateGeneration state_generation;
    ProvenanceGeneration provenance_generation;
    ArtifactGeneration artifact_generation;
    ModelGeneration model_generation;
    AdapterGeneration adapter_generation;
    CompositionGeneration composition_generation;
    PolicyGeneration policy_generation;
    DependencyGeneration dependency_generation;
};

struct StoreSnapshot {
    std::vector<std::shared_ptr<const ProvenanceRecord>> records;  // sorted by subject id
    std::map<StateId, RecordStatus> status;
    AuthoritySnapshot authority;
};

class ProvenanceStore {
public:
    // ---------------------------------------------------------------
    // Publishing
    // ---------------------------------------------------------------
    // Fenced publish: validates the record AND the mutation authority claim.
    PublishOutcome publish(const RecordData& data, const AuthorityClaim& claim) {
        auto err = validate_record(data);
        if (err) return {false, *err, nullptr};

        auto fv = authority_.evaluate_claim(claim);
        if (!fv.accepted())
            return {false, "stale authority [" + fv.dimension + "]: " + fv.detail, nullptr};

        return publish_validated(data);
    }

    // Unfenced publish (in-process convenience).  Still validates structure and
    // graph integrity.
    PublishOutcome publish(const RecordData& data) { return publish_validated(data); }

    // ---------------------------------------------------------------
    // Authority
    // ---------------------------------------------------------------
    std::uint64_t epoch() const {
        std::shared_lock lk(mu_); return authority_.epoch();
    }
    std::uint64_t roll_epoch() {
        std::unique_lock lk(mu_); return authority_.roll_epoch();
    }
    void register_boot(WorkerId w, WorkerBootId b) {
        std::unique_lock lk(mu_); authority_.register_boot(w, b);
    }
    void begin_attempt(AttemptId id, AttemptGeneration g) {
        std::unique_lock lk(mu_); authority_.begin_attempt(id, g);
    }
    void roll_state_generation(StateGeneration g) {
        std::unique_lock lk(mu_); authority_.roll_state_generation(g);
    }
    void roll_provenance_generation(ProvenanceGeneration g) {
        std::unique_lock lk(mu_); authority_.roll_provenance_generation(g);
    }
    void set_model_generation(ModelGeneration g) { std::unique_lock lk(mu_); authority_.set_model_generation(g); }
    void set_adapter_generation(AdapterGeneration g) { std::unique_lock lk(mu_); authority_.set_adapter_generation(g); }
    void set_composition_generation(CompositionGeneration g) { std::unique_lock lk(mu_); authority_.set_composition_generation(g); }
    void set_policy_generation(PolicyGeneration g) { std::unique_lock lk(mu_); authority_.set_policy_generation(g); }
    void set_dependency_generation(DependencyGeneration g) { std::unique_lock lk(mu_); authority_.set_dependency_generation(g); }
    void set_artifact_generation(ArtifactGeneration g) { std::unique_lock lk(mu_); authority_.set_artifact_generation(g); }
    void set_runtime_generation(RuntimeGeneration g) {
        std::unique_lock lk(mu_); runtime_generation_ = g;
    }
    RuntimeGeneration runtime_generation() const { std::shared_lock lk(mu_); return runtime_generation_; }

    FenceVerdict evaluate_claim(const AuthorityClaim& claim) const {
        std::shared_lock lk(mu_); return authority_.evaluate_claim(claim);
    }
    AuthoritySnapshot authority() const {
        std::shared_lock lk(mu_);
        AuthoritySnapshot s;
        s.epoch = authority_.epoch();
        s.attempt_id = authority_.attempt_id();
        s.attempt_generation = authority_.attempt_generation();
        s.state_generation = authority_.state_generation();
        s.provenance_generation = authority_.provenance_generation();
        s.artifact_generation = authority_.artifact_generation();
        s.model_generation = authority_.model_generation();
        s.adapter_generation = authority_.adapter_generation();
        s.composition_generation = authority_.composition_generation();
        s.policy_generation = authority_.policy_generation();
        s.dependency_generation = authority_.dependency_generation();
        // boots
        for (WorkerId w : workers_) {
            auto b = authority_.boot_of(w);
            if (b) s.boots[w] = *b;
        }
        return s;
    }

    // ---------------------------------------------------------------
    // Queries (indexed; O(log n) or graph traversal)
    // ---------------------------------------------------------------
    std::size_t size() const { std::shared_lock lk(mu_); return records_.size(); }
    bool empty() const { return size() == 0; }

    std::shared_ptr<const ProvenanceRecord> find(StateId id) const {
        std::shared_lock lk(mu_);
        auto it = records_.find(id);
        return it == records_.end() ? nullptr : it->second;
    }
    std::shared_ptr<const ProvenanceRecord> find_by_provenance(ProvenanceId pid) const {
        std::shared_lock lk(mu_);
        auto it = prov_to_state_.find(pid);
        if (it == prov_to_state_.end()) return nullptr;
        auto rit = records_.find(it->second);
        return rit == records_.end() ? nullptr : rit->second;
    }

    std::vector<StateId> ids() const {
        std::shared_lock lk(mu_);
        std::vector<StateId> out;
        out.reserve(records_.size());
        for (const auto& [id, r] : records_) out.push_back(id);
        return out;
    }

    std::vector<StateId> by_model(ModelId id) const { return index_query(by_model_, id); }
    std::vector<StateId> by_adapter(AdapterId id) const { return index_query(by_adapter_, id); }
    std::vector<StateId> by_producer(ProducerId id) const { return index_query(by_producer_, id); }
    std::vector<StateId> by_execution(ExecutionId id) const { return index_query(by_execution_, id); }
    std::vector<StateId> by_attempt(AttemptId id) const { return index_query(by_attempt_, id); }
    std::vector<StateId> by_dependency(DependencyId id) const { return index_query(by_dependency_, id); }
    std::vector<StateId> by_artifact_generation(ArtifactGeneration g) const { return index_query(by_artifact_, g); }
    std::vector<StateId> by_validity(ValidityState v) const { return index_query(by_validity_, v); }
    std::vector<StateId> by_evidence(EvidenceClass v) const { return index_query(by_evidence_, v); }

    std::vector<StateId> ancestors(StateId id) const {
        std::shared_lock lk(mu_); return graph_.ancestors(id);
    }
    std::vector<StateId> descendants(StateId id) const {
        std::shared_lock lk(mu_); return graph_.descendants(id);
    }
    std::vector<StateId> parents(StateId id) const {
        std::shared_lock lk(mu_); return graph_.parents(id);
    }
    std::vector<StateId> children(StateId id) const {
        std::shared_lock lk(mu_); return graph_.children(id);
    }
    std::vector<StateId> shared_ancestors(StateId a, StateId b) const {
        std::shared_lock lk(mu_); return graph_.shared_ancestors(a, b);
    }
    std::vector<StateId> roots() const { std::shared_lock lk(mu_); return graph_.roots(); }
    std::vector<StateId> topological_order() const {
        std::shared_lock lk(mu_); return graph_.topological_order();
    }
    bool is_acyclic_dag() const {
        std::shared_lock lk(mu_); return graph_.is_acyclic();
    }

    ValidityState validity_of(StateId id) const {
        std::shared_lock lk(mu_);
        auto it = status_.find(id);
        return it == status_.end() ? ValidityState::UNKNOWN : it->second.validity;
    }

    // ---------------------------------------------------------------
    // Reuse eligibility (field-level + ancestor-validity context)
    // ---------------------------------------------------------------
    ReuseDecision check_reuse(StateId id, const ReuseRequest& req) const {
        std::shared_lock lk(mu_);
        ReuseDecision d;
        auto rit = records_.find(id);
        if (rit == records_.end()) {
            d.eligibility = ReuseEligibility::UNKNOWN;
            d.reasons.push_back("no provenance record for subject " + id.to_string());
            return d;
        }
        // The live status overlay is authoritative for the subject's own
        // current validity (records are immutable).  Check it first.
        auto sit = status_.find(id);
        if (sit != status_.end()) {
            if (sit->second.validity == ValidityState::INVALIDATED) {
                d.eligibility = ReuseEligibility::INVALIDATED;
                d.reasons.clear();
                d.reasons.push_back("subject has been invalidated");
                return d;
            }
            if (sit->second.validity == ValidityState::STALE) {
                d.eligibility = ReuseEligibility::STALE;
                d.reasons.clear();
                d.reasons.push_back("subject is stale");
                return d;
            }
            if (sit->second.validity == ValidityState::UNKNOWN) {
                d.eligibility = ReuseEligibility::UNKNOWN;
                d.reasons.clear();
                d.reasons.push_back("subject validity is unknown");
                return d;
            }
        }

        d = evaluate_reuse(*rit->second, req);

        // Escalate based on ancestor validity (transitive dependency health).
        bool ancestor_invalid = false, ancestor_stale = false;
        auto anc = graph_.ancestors(id);
        for (StateId a : anc) {
            auto s = status_.find(a);
            if (s == status_.end()) continue;
            if (s->second.validity == ValidityState::INVALIDATED) ancestor_invalid = true;
        }
        if (ancestor_invalid) {
            d.eligibility = ReuseEligibility::INVALIDATED;
            d.failed_dimensions.clear();
            d.reasons.clear();
            d.reasons.push_back("an ancestor of this subject has been invalidated");
        } else {
            for (StateId a : anc) {
                auto s = status_.find(a);
                if (s == status_.end()) continue;
                if (s->second.validity == ValidityState::STALE) ancestor_stale = true;
            }
            if (ancestor_stale && d.eligibility != ReuseEligibility::INVALIDATED) {
                d.eligibility = ReuseEligibility::STALE;
                d.failed_dimensions.clear();
                d.reasons.clear();
                d.reasons.push_back("an ancestor of this subject is stale");
            }
        }
        return d;
    }

    // ---------------------------------------------------------------
    // Invalidation (with deterministic propagation)
    // ---------------------------------------------------------------
    InvalidationOutcome invalidate(const InvalidatingSubject& subj) {
        std::unique_lock lk(mu_);
        InvalidationOutcome out;
        if (records_.find(subj.subject) == records_.end()) {
            out.error = "invalidating unknown subject " + subj.subject.to_string();
            return out;
        }
        InvalidationResult res = propagate_invalidation(graph_, {subj});
        for (StateId affected : res.affected) {
            ValidityState oldv = ValidityState::VALID;
            auto it = status_.find(affected);
            if (it != status_.end()) oldv = it->second.validity;
            auto& st = status_[affected];
            st.validity = ValidityState::INVALIDATED;
            StateId root = res.root_of.count(affected) ? res.root_of.at(affected) : affected;
            st.reason = res.root_reason.count(root) ? res.root_reason.at(root) : subj.reason;
            if (affected == subj.subject) {
                st.detail = subj.detail.empty() ? "invalidated by operator" : subj.detail;
            } else {
                st.detail = "invalidated because it transitively depends on " + root.to_string();
            }
            if (oldv != ValidityState::INVALIDATED) {
                by_validity_[oldv].erase(affected);
                by_validity_[ValidityState::INVALIDATED].insert(affected);
            }
        }
        out.ok = true;
        out.result = std::move(res);
        return out;
    }

    // ---------------------------------------------------------------
    // Snapshot / restore (for persistence; path kept out of the lock)
    // ---------------------------------------------------------------
    StoreSnapshot snapshot() const {
        std::shared_lock lk(mu_);
        StoreSnapshot s;
        for (const auto& [id, r] : records_) s.records.push_back(r);
        s.status = status_;
        s.authority = authority_snapshot_locked();
        return s;
    }

    void restore(const StoreSnapshot& s) {
        std::unique_lock lk(mu_);
        records_.clear();
        prov_to_state_.clear();
        graph_ = ProvenanceGraph{};
        clear_indexes();
        status_.clear();
        workers_.clear();

        // Rebuild authority exactly as persisted.
        authority_ = AuthorityState{};
        authority_.set_epoch(s.authority.epoch);
        authority_.begin_attempt(s.authority.attempt_id, s.authority.attempt_generation);
        authority_.roll_state_generation(s.authority.state_generation);
        authority_.roll_provenance_generation(s.authority.provenance_generation);
        authority_.set_artifact_generation(s.authority.artifact_generation);
        authority_.set_model_generation(s.authority.model_generation);
        authority_.set_adapter_generation(s.authority.adapter_generation);
        authority_.set_composition_generation(s.authority.composition_generation);
        authority_.set_policy_generation(s.authority.policy_generation);
        authority_.set_dependency_generation(s.authority.dependency_generation);
        for (const auto& [w, b] : s.authority.boots) {
            authority_.register_boot(w, b);
            workers_.insert(w);
        }

        // Insert records in deterministic topological order so that every parent
        // is present before its child (required by insert_validated).
        auto order = records_in_topo_order(s);
        if (order.size() != s.records.size()) {
            throw std::runtime_error("restore refused: persisted provenance graph contains a cycle");
        }
        for (const auto& rec : order) {
            auto data = rec->data();
            insert_validated(data);
        }
        // After insertion, restore the live status overlay (validity/invalidation).
        status_ = s.status;
        // Maintain the current-validity index against the restored overlay.
        by_validity_.clear();
        for (const auto& [id, st] : status_) by_validity_[st.validity].insert(id);
        for (const auto& [id, rec] : records_) {
            if (!status_.count(id)) by_validity_[ValidityState::VALID].insert(id);
        }
    }

    std::string store_digest(const StoreSnapshot& s) const {
        digest::StableDigest sd(0x73746f7265ull); // "store"
        for (const auto& rec : s.records) {
            sd.with(rec->d.subject_id).with(rec->d.provenance_id)
              .with(rec->derivation_digest());
        }
        sd.with(static_cast<std::uint64_t>(s.records.size()));
        for (const auto& [id, st] : s.status) {
            sd.with(id).with(static_cast<int>(st.validity))
              .with(st.detail);
        }
        sd.with(s.authority.epoch);
        return sd.str();
    }

private:
    PublishOutcome publish_validated(const RecordData& data) {
        std::unique_lock lk(mu_);
        auto err = validate_record(data);
        if (err) return {false, *err, nullptr};
        return insert_validated(data);
    }

    PublishOutcome insert_validated(const RecordData& data) {
        // Duplicate identity rejection
        if (records_.count(data.subject_id)) {
            return {false, "duplicate StateId " + data.subject_id.to_string(), nullptr};
        }
        if (prov_to_state_.count(data.provenance_id)) {
            return {false, "duplicate ProvenanceId " + data.provenance_id.to_string(), nullptr};
        }
        // Parent freshness: every input_state and parent_provenance must exist.
        for (StateId s : data.input_states) {
            if (!records_.count(s)) {
                return {false, "dangling input_state " + s.to_string() + " (parents must exist)", nullptr};
            }
        }
        for (ProvenanceId p : data.parent_provenance) {
            auto it = prov_to_state_.find(p);
            if (it == prov_to_state_.end()) {
                return {false, "dangling parent_provenance " + p.to_string(), nullptr};
            }
        }
        // Build a frozen record.
        auto rec = std::make_shared<const ProvenanceRecord>(data);
        // Graph: node + arcs (parent -> subject), dedup parents.
        graph_.add_node(data.subject_id);
        std::set<StateId> parent_set;
        for (StateId s : data.input_states) parent_set.insert(s);
        for (ProvenanceId p : data.parent_provenance) parent_set.insert(prov_to_state_.at(p));
        for (StateId parent : parent_set) {
            try {
                graph_.add_arc(parent, data.subject_id);
            } catch (const GraphError& e) {
                return {false, std::string("graph rejection: ") + e.what(), nullptr};
            }
        }
        // Indexing
        index_insert(data, data.subject_id);
        records_[data.subject_id] = rec;
        prov_to_state_[data.provenance_id] = data.subject_id;
        status_[data.subject_id] = RecordStatus{
            data.validity, data.invalidation_reason, data.provenance_generation, data.note};
        if (data.worker_id.valid()) workers_.insert(data.worker_id);
        return {true, "", rec};
    }

    template <typename K>
    std::vector<StateId> index_query(const std::map<K, std::set<StateId>>& idx, K key) const {
        std::shared_lock lk(mu_);
        auto it = idx.find(key);
        if (it == idx.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    void index_insert(const RecordData& d, StateId sid) {
        if (d.model_id.valid())   by_model_[d.model_id].insert(sid);
        if (d.adapter_id.valid()) by_adapter_[d.adapter_id].insert(sid);
        if (d.producer_id.valid()) by_producer_[d.producer_id].insert(sid);
        if (d.execution_id.valid()) by_execution_[d.execution_id].insert(sid);
        if (d.attempt_id.valid()) by_attempt_[d.attempt_id].insert(sid);
        for (const auto& dep : d.dependencies)
            if (dep.id.valid()) by_dependency_[dep.id].insert(sid);
        if (d.artifact_generation.valid()) by_artifact_[d.artifact_generation].insert(sid);
        by_validity_[d.validity].insert(sid);
        by_evidence_[d.evidence].insert(sid);
    }

    void clear_indexes() {
        by_model_.clear(); by_adapter_.clear(); by_producer_.clear();
        by_execution_.clear(); by_attempt_.clear(); by_dependency_.clear();
        by_artifact_.clear(); by_validity_.clear(); by_evidence_.clear();
    }

    // Deterministic topological ordering of a snapshot's records (parents first).
    static std::vector<std::shared_ptr<const ProvenanceRecord>>
    records_in_topo_order(const StoreSnapshot& s) {
        std::map<ProvenanceId, StateId> prov_map;
        for (const auto& rec : s.records) prov_map[rec->d.provenance_id] = rec->d.subject_id;

        std::map<StateId, std::set<StateId>> parents_map;
        std::map<StateId, std::size_t> indeg;
        for (const auto& rec : s.records) { parents_map[rec->d.subject_id]; indeg[rec->d.subject_id]; }
        for (const auto& rec : s.records) {
            StateId sid = rec->d.subject_id;
            std::set<StateId> ps;
            for (StateId st : rec->d.input_states) ps.insert(st);
            for (ProvenanceId p : rec->d.parent_provenance) {
                auto it = prov_map.find(p);
                if (it != prov_map.end()) ps.insert(it->second);
            }
            parents_map[sid] = ps;
            indeg[sid] = ps.size();
        }
        std::set<StateId> ready;
        for (const auto& [id, d] : indeg) if (d == 0) ready.insert(id);
        // children adjacency for O(E) decrements
        std::map<StateId, std::vector<StateId>> children_of;
        for (const auto& [cid, ps] : parents_map)
            for (StateId p : ps) children_of[p].push_back(cid);
        std::vector<StateId> order;
        while (!ready.empty()) {
            StateId cur = *ready.begin(); ready.erase(ready.begin());
            order.push_back(cur);
            for (StateId ch : children_of[cur]) {
                if (--indeg[ch] == 0) ready.insert(ch);
            }
        }
        std::vector<std::shared_ptr<const ProvenanceRecord>> out;
        if (order.size() != s.records.size()) {
            // Malformed cycle in persisted snapshot: refuse to restore anything.
            return {};
        }
        std::map<StateId, std::shared_ptr<const ProvenanceRecord>> by_id;
        for (const auto& rec : s.records) by_id[rec->d.subject_id] = rec;
        for (StateId id : order) out.push_back(by_id.at(id));
        return out;
    }

    AuthoritySnapshot authority_snapshot_locked() const {
        AuthoritySnapshot s;
        s.epoch = authority_.epoch();
        s.attempt_id = authority_.attempt_id();
        s.attempt_generation = authority_.attempt_generation();
        s.state_generation = authority_.state_generation();
        s.provenance_generation = authority_.provenance_generation();
        s.artifact_generation = authority_.artifact_generation();
        s.model_generation = authority_.model_generation();
        s.adapter_generation = authority_.adapter_generation();
        s.composition_generation = authority_.composition_generation();
        s.policy_generation = authority_.policy_generation();
        s.dependency_generation = authority_.dependency_generation();
        for (WorkerId w : workers_) {
            auto b = authority_.boot_of(w);
            if (b) s.boots[w] = *b;
        }
        return s;
    }

    mutable std::shared_mutex mu_;
    std::map<StateId, std::shared_ptr<const ProvenanceRecord>> records_;
    std::map<ProvenanceId, StateId> prov_to_state_;
    ProvenanceGraph graph_;
    std::map<StateId, RecordStatus> status_;
    AuthorityState authority_;
    std::set<WorkerId> workers_;
    RuntimeGeneration runtime_generation_;

    std::map<ModelId, std::set<StateId>> by_model_;
    std::map<AdapterId, std::set<StateId>> by_adapter_;
    std::map<ProducerId, std::set<StateId>> by_producer_;
    std::map<ExecutionId, std::set<StateId>> by_execution_;
    std::map<AttemptId, std::set<StateId>> by_attempt_;
    std::map<DependencyId, std::set<StateId>> by_dependency_;
    std::map<ArtifactGeneration, std::set<StateId>> by_artifact_;
    std::map<ValidityState, std::set<StateId>> by_validity_;
    std::map<EvidenceClass, std::set<StateId>> by_evidence_;
};

} // namespace stateprovenance
