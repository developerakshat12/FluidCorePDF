#pragma once

#include "RTreeIndex.h"

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

    WorkspaceNode* find(const std::string& nodeId) const;
    Point positionOf(const std::string& nodeId) const;
    Rectangle boundsOf(const std::string& nodeId) const;

    // Nodes whose bounds intersect the viewport rectangle.
    std::vector<WorkspaceNode*> visibleIn(const Rectangle& viewport) const;

    // Union bounding box of all nodes in the workspace, or zero rectangle if empty.
    Rectangle globalBounds() const;

    // All registered node IDs.
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
