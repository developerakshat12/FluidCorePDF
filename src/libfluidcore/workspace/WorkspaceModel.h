#pragma once

#include "RTreeIndex.h"
#include "workspace/CardStackNode.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace FluidCore {

// Spatial scene graph over WorkspaceNode entities, indexed by RTreeIndex
// (TRD §3.4). Node identity comes from WorkspaceNode::id() — the frontend mints
// the UUIDs so ids stay stable across the future SQLite store (TRD §4.1).
class WorkspaceModel {
  public:
    explicit WorkspaceModel(std::string projectId);

    const std::string& projectId() const { return m_projectId; }
    std::size_t nodeCount() const { return m_nodes.size(); }

    // Takes ownership and registers the node under its own id(). Returns "" for
    // a null node, an empty id, or a duplicate id.
    std::string insert(std::unique_ptr<WorkspaceNode> node);

    bool remove(const std::string& nodeId);

    // Moves a node's origin (top-left) to (x, y); its size is preserved.
    bool move(const std::string& nodeId, double x, double y);

    // Updates spatial index bounds when a node's internal size changes (e.g. stack expand/collapse)
    bool updateBounds(const std::string& nodeId);

    WorkspaceNode* find(const std::string& nodeId) const;

    // Finds a node either at top level or nested recursively within a CardStackNode.
    WorkspaceNode* findRecursive(const std::string& nodeId) const;

    // Returns the parent CardStackNode containing childId, or nullptr if top-level/absent.
    CardStackNode* stackContainingNode(const std::string& childId) const;

    Point positionOf(const std::string& nodeId) const;

    // Resolves accurate bounds:
    // - Top-level node -> its own bounds.
    // - Child inside an expanded stack -> child's active bounds.
    // - Child inside a collapsed stack -> composite stack bounds.
    Rectangle boundsOf(const std::string& nodeId) const;

    // Auto-dissolves a stack if it contains 1 or 0 children, promoting remaining child to root.
    bool dissolveStackIfSingleChild(const std::string& stackId,
                                    std::string* extractedChildId = nullptr);

    // Nodes whose bounds intersect the viewport rectangle.
    std::vector<WorkspaceNode*> visibleIn(const Rectangle& viewport) const;

    // Union bounding box of all nodes in the workspace, or zero rectangle if empty.
    Rectangle globalBounds() const;

    // All registered top-level node IDs.
    std::vector<std::string> allNodeIds() const;

  private:
    struct Record {
        std::unique_ptr<WorkspaceNode> node;
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        RTreeIndex::Handle handle = RTreeIndex::kInvalidHandle;
    };

    std::string m_projectId;
    RTreeIndex m_index;
    std::unordered_map<std::string, Record> m_nodes;
    std::unordered_map<RTreeIndex::Handle, std::string> m_idByHandle;
};

} // namespace FluidCore
