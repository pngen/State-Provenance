#pragma once
// ---------------------------------------------------------------------------
// State Provenance - deterministic explainability.
// Provides human-readable text, JSON, graph/path explanations and stable
// digests for provenance records, reuse decisions and invalidation results.
// All output is deterministic (sorted).
// ---------------------------------------------------------------------------
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/store.hpp"
#include "stateprovenance/compat.hpp"
#include "stateprovenance/invalidate.hpp"

namespace stateprovenance::explain {

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out.push_back('\\'); out.push_back('"'); break;
            case '\\': out.push_back('\\'); out.push_back('\\'); break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<int>(static_cast<unsigned char>(c)));
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

inline std::string record_json(const RecordData& d) {
    std::ostringstream o;
    o << "{";
    o << "\"provenance_id\":\"" << d.provenance_id.to_string() << "\",";
    o << "\"provenance_generation\":\"" << d.provenance_generation.to_string() << "\",";
    o << "\"subject_id\":\"" << d.subject_id.to_string() << "\",";
    o << "\"subject_kind\":\"" << name_of(d.subject_kind) << "\",";
    o << "\"state_generation\":\"" << d.state_generation.to_string() << "\",";
    o << "\"producer_id\":\"" << d.producer_id.to_string() << "\",";
    o << "\"producer_generation\":\"" << d.producer_generation.to_string() << "\",";
    o << "\"execution_id\":\"" << d.execution_id.to_string() << "\",";
    o << "\"attempt_id\":\"" << d.attempt_id.to_string() << "\",";
    o << "\"created_at\":" << d.created_at << ",";
    o << "\"model_id\":\"" << d.model_id.to_string() << "\",";
    o << "\"model_generation\":\"" << d.model_generation.to_string() << "\",";
    o << "\"model_revision\":\"" << json_escape(d.model_revision) << "\",";
    o << "\"tokenizer\":\"" << json_escape(d.tokenizer) << "\",";
    o << "\"adapter_id\":\"" << d.adapter_id.to_string() << "\",";
    o << "\"adapter_generation\":\"" << d.adapter_generation.to_string() << "\",";
    o << "\"adapter_revision\":\"" << json_escape(d.adapter_revision) << "\",";
    o << "\"composition_id\":\"" << d.composition_id.to_string() << "\",";
    o << "\"composition_generation\":\"" << d.composition_generation.to_string() << "\",";
    o << "\"composition_fingerprint\":\"" << json_escape(d.composition_fingerprint) << "\",";
    o << "\"runtime_id\":\"" << d.runtime_id.to_string() << "\",";
    o << "\"runtime_generation\":\"" << d.runtime_generation.to_string() << "\",";
    o << "\"artifact_generation\":\"" << d.artifact_generation.to_string() << "\",";
    o << "\"backend_id\":\"" << d.backend_id.to_string() << "\",";
    o << "\"toolchain_id\":\"" << d.toolchain_id.to_string() << "\",";
    o << "\"device_id\":\"" << d.device_id.to_string() << "\",";
    o << "\"architecture\":\"" << json_escape(d.architecture) << "\",";
    o << "\"compute_capability\":\"" << json_escape(d.compute_capability) << "\",";
    o << "\"abi\":\"" << json_escape(d.abi) << "\",";
    o << "\"dtype\":\"" << json_escape(d.dtype) << "\",";
    o << "\"layout\":\"" << json_escape(d.layout) << "\",";
    o << "\"shape\":\"" << json_escape(d.shape) << "\",";
    o << "\"content_digest\":\"" << json_escape(d.content_digest) << "\",";
    o << "\"policy_id\":\"" << d.policy_id.to_string() << "\",";
    o << "\"policy_generation\":\"" << d.policy_generation.to_string() << "\",";
    o << "\"coordinator_epoch\":" << d.coordinator_epoch << ",";
    o << "\"worker_id\":\"" << d.worker_id.to_string() << "\",";
    o << "\"worker_boot\":\"" << d.worker_boot.to_string() << "\",";
    o << "\"evidence\":\"" << name_of(d.evidence) << "\",";
    o << "\"validity\":\"" << name_of(d.validity) << "\",";
    o << "\"invalidation_reason\":\"" << name_of(d.invalidation_reason) << "\",";
    o << "\"reuse_eligibility\":\"" << name_of(d.reuse_eligibility) << "\",";
    o << "\"input_states\":[";
    for (std::size_t i = 0; i < d.input_states.size(); ++i) {
        if (i) o << ",";
        o << "\"" << d.input_states[i].to_string() << "\"";
    }
    o << "],";
    o << "\"parent_provenance\":[";
    for (std::size_t i = 0; i < d.parent_provenance.size(); ++i) {
        if (i) o << ",";
        o << "\"" << d.parent_provenance[i].to_string() << "\"";
    }
    o << "],";
    o << "\"dependencies\":[";
    for (std::size_t i = 0; i < d.dependencies.size(); ++i) {
        const auto& dep = d.dependencies[i];
        if (i) o << ",";
        o << "{\"id\":\"" << dep.id.to_string() << "\",\"generation\":\""
          << dep.generation.to_string() << "\",\"kind\":\"" << json_escape(dep.kind)
          << "\",\"revision\":\"" << json_escape(dep.revision) << "\"}";
    }
    o << "],";
    o << "\"requirements\":[";
    for (std::size_t i = 0; i < d.requirements.size(); ++i) {
        const auto& req = d.requirements[i];
        if (i) o << ",";
        o << "{\"dimension\":\"" << name_of(req.dimension) << "\",\"value\":\""
          << json_escape(req.required_value) << "\",\"note\":\"" << json_escape(req.note) << "\"}";
    }
    o << "],";
    o << "\"note\":\"" << json_escape(d.note) << "\"";
    o << "}";
    return o.str();
}

inline std::string record_text(const RecordData& d) {
    std::ostringstream o;
    o << "State " << d.subject_id.to_string()
      << " (" << name_of(d.subject_kind) << ")"
      << " [state generation " << d.state_generation.to_string() << "]";
    if (d.model_id.valid())
        o << "\n  model: " << d.model_id.to_string()
          << " gen " << d.model_generation.to_string()
          << " revision '" << d.model_revision << "'";
    if (d.adapter_id.valid())
        o << "\n  adapter: " << d.adapter_id.to_string()
          << " gen " << d.adapter_generation.to_string() << " revision '" << d.adapter_revision << "'";
    if (d.composition_id.valid())
        o << "\n  composition: " << d.composition_id.to_string()
          << " fp '" << d.composition_fingerprint << "'";
    if (d.runtime_id.valid())
        o << "\n  runtime: " << d.runtime_id.to_string()
          << " backend " << d.backend_id.to_string() << " toolchain " << d.toolchain_id.to_string();
    if (!d.architecture.empty() || !d.compute_capability.empty())
        o << "\n  device: " << d.device_id.to_string() << " arch '" << d.architecture
          << "' cc '" << d.compute_capability << "'";
    if (!d.dtype.empty())
        o << "\n  tensor: dtype '" << d.dtype << "' layout '" << d.layout << "' shape '" << d.shape << "'";
    o << "\n  producer: " << d.producer_id.to_string()
      << " gen " << d.producer_generation.to_string()
      << " execution " << d.execution_id.to_string();
    if (!d.input_states.empty()) {
        o << "\n  inputs:";
        for (const auto& s : d.input_states) o << " " << s.to_string();
    }
    if (!d.parent_provenance.empty()) {
        o << "\n  parents:";
        for (const auto& s : d.parent_provenance) o << " " << s.to_string();
    }
    if (!d.dependencies.empty()) {
        o << "\n  dependencies:";
        for (const auto& dep : d.dependencies)
            o << " " << dep.id.to_string() << "(" << dep.kind << ")";
    }
    if (!d.requirements.empty()) {
        o << "\n  requirements:";
        for (const auto& req : d.requirements)
            o << " " << name_of(req.dimension) << "=" << req.required_value;
    }
    o << "\n  evidence: " << name_of(d.evidence)
      << " validity: " << name_of(d.validity)
      << " provenance_id " << d.provenance_id.to_string()
      << " gen " << d.provenance_generation.to_string();
    return o.str();
}

inline std::string reuse_text(const ReuseDecision& d) {
    return d.summary();
}

inline std::string chain_text(const ProvenanceStore& store, StateId id) {
    auto ancestors = store.ancestors(id);
    std::ostringstream o;
    o << "Derivation path to " << id.to_string() << ":";
    if (ancestors.empty()) { o << " (root)"; return o.str(); }
    for (StateId a : ancestors) o << "\n  <- " << a.to_string();
    return o.str();
}

inline std::string invalidation_text(const InvalidationResult& r) {
    std::ostringstream o;
    o << "Invalidated " << r.affected.size() << " subject(s).";
    o << "\nCausal chains:";
    for (StateId t : r.affected) {
        auto ch = r.chain_for(t);
        o << "\n  " << ch.root.to_string();
        for (StateId s : ch.path) o << " -> " << s.to_string();
        o << " (" << name_of(ch.reason) << ")";
    }
    return o.str();
}

inline std::string store_json(const ProvenanceStore& s) {
    std::ostringstream o;
    o << "{\"records\":[";
    auto ids = s.ids();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto rec = s.find(ids[i]);
        if (!rec) continue;
        if (i) o << ",";
        o << record_json(rec->d);
    }
    o << "],\"size\":" << s.size() << "}";
    return o.str();
}

} // namespace stateprovenance::explain
