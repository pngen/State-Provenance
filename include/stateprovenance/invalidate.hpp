#pragma once
// ---------------------------------------------------------------------------
// State Provenance - deterministic invalidation propagation.
//
// Invalidating a subject invalidates the transitive closure of its descendants
// (anything that depends on invalid state).  Propagation is deterministic:
// roots are processed in sorted order and neighbours are expanded in sorted
// order, so the affected set and causal structures are reproducible.  Unrelated
// branches are never touched.
//
// Causal paths are reconstructed lazily per target (O(depth) each) so that
// invalidation impact analysis is O(affected + edges) rather than O(affected *
// depth).  Exact chains remain available via path_to()/chain_for().
// ---------------------------------------------------------------------------
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/graph.hpp"

namespace stateprovenance {

struct InvalidatingSubject {
    StateId subject;
    InvalidationReason reason = InvalidationReason::Unknown;
    std::string detail;
};

struct InvalidationChain {
    StateId root;
    InvalidationReason reason;
    std::string detail;
    std::vector<StateId> path;   // path[0]==root, path.back()==target
};

struct InvalidationResult {
    std::vector<StateId> affected;                        // roots + descendants, sorted
    std::map<StateId, StateId> predecessor;              // target -> previous (root->root)
    std::map<StateId, StateId> root_of;                  // target -> root
    std::map<StateId, InvalidationReason> root_reason;   // root -> reason
    std::map<StateId, std::string> root_detail;          // root -> detail

    bool contains(StateId id) const {
        return std::binary_search(affected.begin(), affected.end(), id);
    }

    std::size_t size() const { return affected.size(); }

    // Lazily reconstruct the causal path from the root to target.
    std::vector<StateId> path_to(StateId target) const {
        std::vector<StateId> path;
        StateId cur = target;
        std::size_t guard = 0;
        while (true) {
            path.push_back(cur);
            auto it = predecessor.find(cur);
            if (it == predecessor.end()) break;
            if (it->second == cur) break;
            cur = it->second;
            if (++guard > affected.size() + 1) break;   // safety: cycle in predecessor
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    InvalidationChain chain_for(StateId target) const {
        InvalidationChain ch;
        auto rit = root_of.find(target);
        if (rit == root_of.end()) { ch.root = target; return ch; }
        ch.root = rit->second;
        auto rrt = root_reason.find(ch.root);
        ch.reason = (rrt == root_reason.end()) ? InvalidationReason::Unknown : rrt->second;
        auto rdt = root_detail.find(ch.root);
        ch.detail = (rdt == root_detail.end()) ? "" : rdt->second;
        ch.path = path_to(target);
        return ch;
    }
};

inline InvalidationResult propagate_invalidation(const ProvenanceGraph& graph,
                                                 const std::vector<InvalidatingSubject>& roots) {
    InvalidationResult result;

    // Deterministic root ordering + dedupe (first reason/detail wins).
    std::map<StateId, InvalidationReason> root_reason;
    std::map<StateId, std::string> root_detail;
    for (const auto& r : roots) {
        if (!root_reason.count(r.subject)) {
            root_reason[r.subject] = r.reason;
            root_detail[r.subject] = r.detail;
        }
    }

    // BFS from roots in sorted order; deterministic predecessor/root assignment.
    std::map<StateId, StateId> predecessor, root_of;
    std::vector<StateId> queue;
    for (const auto& [id, reason] : root_reason) {
        predecessor[id] = id;
        root_of[id] = id;
        queue.push_back(id);
    }
    std::size_t head = 0;
    while (head < queue.size()) {
        StateId cur = queue[head++];
        StateId cur_root = root_of[cur];
        for (StateId child : graph.children(cur)) {
            if (!predecessor.count(child)) {
                predecessor[child] = cur;
                root_of[child] = cur_root;
                queue.push_back(child);
            }
        }
    }

    result.affected = queue;
    std::sort(result.affected.begin(), result.affected.end());
    result.predecessor = std::move(predecessor);
    result.root_of = std::move(root_of);
    result.root_reason = std::move(root_reason);
    result.root_detail = std::move(root_detail);
    return result;
}

} // namespace stateprovenance
