#pragma once
// ---------------------------------------------------------------------------
// State Provenance - explicit directed derivation/provenance graph.
//
// Nodes are StateIds; arcs point parent -> child (the parent is an input from
// which the child was derived).  The graph is a strict DAG.  It rejects:
//   - self-dependency
//   - duplicate edges
//   - cycles (illegal)
//   - impossible ancestry
// All traversal/ordering is deterministic (sorted by StateId).
// ---------------------------------------------------------------------------
#include <algorithm>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "stateprovenance/ids.hpp"

namespace stateprovenance {

class GraphError : public std::runtime_error {
public:
    explicit GraphError(const std::string& msg) : std::runtime_error(msg) {}
};

class ProvenanceGraph {
public:
    // Returns false if the node already existed.
    bool add_node(StateId id) {
        if (!id.valid()) throw GraphError("graph node id must be non-zero");
        if (nodes_.count(id)) return false;
        nodes_.insert(id);
        parents_[id];   // create empty
        children_[id];  // create empty
        return true;
    }

    bool has_node(StateId id) const { return nodes_.count(id) != 0; }
    std::size_t node_count() const { return nodes_.size(); }
    std::size_t arc_count() const { return arc_count_; }

    // Adds parent -> child.  Rejects self-dependency, duplicate edges and
    // cycles.  Both endpoints must already exist (call add_node first).
    bool add_arc(StateId parent, StateId child) {
        if (!parent.valid() || !child.valid())
            throw GraphError("arc endpoints must be non-zero");
        if (parent == child)
            throw GraphError("self-dependency is not permitted");
        if (!has_node(parent) || !has_node(child))
            throw GraphError("arc endpoints must be registered nodes");
        if (children_[parent].count(child))
            throw GraphError("duplicate arc");
        if (would_create_cycle(parent, child))
            throw GraphError("cycle would be created by arc parent->child");

        children_[parent].insert(child);
        parents_[child].insert(parent);
        ++arc_count_;
        return true;
    }

    // Parents (direct inputs) of a node, sorted.
    std::vector<StateId> parents(StateId id) const {
        auto it = parents_.find(id);
        if (it == parents_.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    // Children (direct derivatives) of a node, sorted.
    std::vector<StateId> children(StateId id) const {
        auto it = children_.find(id);
        if (it == children_.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    // Transitive ancestors (all upstream inputs), sorted. Excludes self.
    std::vector<StateId> ancestors(StateId id) const {
        std::set<StateId> out;
        if (!has_node(id)) return {};
        std::vector<StateId> stack = parents(id);
        while (!stack.empty()) {
            StateId cur = stack.back(); stack.pop_back();
            if (out.insert(cur).second) {
                auto p = parents(cur);
                stack.insert(stack.end(), p.begin(), p.end());
            }
        }
        return {out.begin(), out.end()};
    }

    // Transitive descendants (all downstream derivatives), sorted. Excludes self.
    std::vector<StateId> descendants(StateId id) const {
        std::set<StateId> out;
        if (!has_node(id)) return {};
        std::vector<StateId> stack = children(id);
        while (!stack.empty()) {
            StateId cur = stack.back(); stack.pop_back();
            if (out.insert(cur).second) {
                auto c = children(cur);
                stack.insert(stack.end(), c.begin(), c.end());
            }
        }
        return {out.begin(), out.end()};
    }

    // Roots: nodes with no parents, sorted.
    std::vector<StateId> roots() const {
        std::vector<StateId> out;
        for (const auto& id : nodes_) if (parents_.at(id).empty()) out.push_back(id);
        return out;
    }

    // Shared ancestors of two nodes, sorted.
    std::vector<StateId> shared_ancestors(StateId a, StateId b) const {
        auto aa = ancestors(a);
        auto ab = ancestors(b);
        std::vector<StateId> out;
        std::set_intersection(aa.begin(), aa.end(), ab.begin(), ab.end(),
                              std::back_inserter(out));
        return out;
    }

    std::size_t fan_in(StateId id) const { return parents(id).size(); }
    std::size_t fan_out(StateId id) const { return children(id).size(); }

    // Deterministic topological order (Kahn's algorithm, sorted tie-break).
    // Throws GraphError if the graph is not a DAG.
    std::vector<StateId> topological_order() const {
        std::set<StateId> indeg;
        for (const auto& id : nodes_) indeg.insert(id);
        std::vector<StateId> result;
        result.reserve(nodes_.size());
        // compute in-degree
        std::map<StateId, std::size_t> in_deg;  // use std::map for full order
        for (const auto& id : nodes_) in_deg[id] = parents_.at(id).size();
        std::set<StateId> ready;   // zero in-degree, sorted
        for (const auto& [id, d] : in_deg) if (d == 0) ready.insert(id);
        while (!ready.empty()) {
            StateId cur = *ready.begin(); ready.erase(ready.begin());
            result.push_back(cur);
            for (StateId ch : children(cur)) {
                if (--in_deg[ch] == 0) ready.insert(ch);
            }
        }
        if (result.size() != nodes_.size())
            throw GraphError("graph contains a cycle");
        return result;
    }

    // Node set (sorted).
    std::vector<StateId> nodes() const { return {nodes_.begin(), nodes_.end()}; }

    // Deterministic adjacency list for serialization/explanations.
    struct Arc { StateId parent; StateId child; };
    std::vector<Arc> arcs() const {
        std::vector<Arc> out;
        for (const auto& id : nodes_) {
            for (StateId ch : children(id)) out.push_back({id, ch});
        }
        std::sort(out.begin(), out.end(), [](const Arc& a, const Arc& b) {
            if (a.parent != b.parent) return a.parent < b.parent;
            return a.child < b.child;
        });
        return out;
    }

    bool is_acyclic() const {
        try { topological_order(); return true; }
        catch (const GraphError&) { return false; }
    }

private:
    // Would adding parent->child create a cycle?
    // A cycle forms iff child can already reach parent through existing arcs
    // (i.e. parent is currently a descendant of child).
    bool would_create_cycle(StateId parent, StateId child) const {
        // BFS from child following the children adjacency; if we reach parent,
        // a cycle would be introduced.
        std::set<StateId> visited;
        std::vector<StateId> stack{child};
        while (!stack.empty()) {
            StateId cur = stack.back(); stack.pop_back();
            auto it = children_.find(cur);
            if (it == children_.end()) continue;
            for (StateId nxt : it->second) {
                if (nxt == parent) return true;
                if (visited.insert(nxt).second) stack.push_back(nxt);
            }
        }
        return false;
    }

    std::set<StateId> nodes_;
    std::map<StateId, std::set<StateId>> parents_;
    std::map<StateId, std::set<StateId>> children_;
    std::size_t arc_count_ = 0;
};

} // namespace stateprovenance
