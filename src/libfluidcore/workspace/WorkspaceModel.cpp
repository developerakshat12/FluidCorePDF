#include "WorkspaceModel.h"

#include <utility>

namespace FluidCore {

WorkspaceModel::WorkspaceModel(std::string projectId) : m_projectId(std::move(projectId)) {}

std::string WorkspaceModel::insert(std::unique_ptr<WorkspaceNode> node) {
    if (!node)
        return {};

    const std::string id = node->id();
    if (id.empty() || m_nodes.count(id) != 0u)
        return {};

    const Rectangle bounds = node->bounds();
    Record record;
    record.node = std::move(node);
    record.x = bounds.x;
    record.y = bounds.y;
    record.width = bounds.w;
    record.height = bounds.h;
    record.handle = m_index.insert(bounds);
    m_idByHandle.emplace(record.handle, id);
    m_nodes.emplace(id, std::move(record));
    return id;
}

bool WorkspaceModel::remove(const std::string& nodeId) {
    const auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end())
        return false;

    m_index.remove(it->second.handle);
    m_idByHandle.erase(it->second.handle);
    m_nodes.erase(it);
    return true;
}

bool WorkspaceModel::move(const std::string& nodeId, double x, double y) {
    const auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end())
        return false;

    Record& record = it->second;
    record.x = x;
    record.y = y;
    m_index.update(record.handle, {x, y, record.width, record.height});
    return true;
}

WorkspaceNode* WorkspaceModel::find(const std::string& nodeId) const {
    const auto it = m_nodes.find(nodeId);
    return it == m_nodes.end() ? nullptr : it->second.node.get();
}

Point WorkspaceModel::positionOf(const std::string& nodeId) const {
    const Rectangle bounds = boundsOf(nodeId);
    return {bounds.x, bounds.y};
}

Rectangle WorkspaceModel::boundsOf(const std::string& nodeId) const {
    const auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end())
        return {};
    const Record& record = it->second;
    return {record.x, record.y, record.width, record.height};
}

std::vector<WorkspaceNode*> WorkspaceModel::visibleIn(const Rectangle& viewport) const {
    std::vector<WorkspaceNode*> out;
    for (const RTreeIndex::Handle handle : m_index.query(viewport)) {
        const auto idIt = m_idByHandle.find(handle);
        if (idIt == m_idByHandle.end())
            continue;
        const auto nodeIt = m_nodes.find(idIt->second);
        if (nodeIt == m_nodes.end())
            continue;
        out.push_back(nodeIt->second.node.get());
    }
    return out;
}

} // namespace FluidCore
