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
    if (record.node) {
        record.node->setPosition(x, y);
        const Rectangle b = record.node->bounds();
        record.width = b.w;
        record.height = b.h;
    }
    m_index.update(record.handle, {x, y, record.width, record.height});
    return true;
}

bool WorkspaceModel::updateBounds(const std::string& nodeId) {
    const auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end())
        return false;

    Record& record = it->second;
    const Rectangle b = record.node->bounds();
    record.x = b.x;
    record.y = b.y;
    record.width = b.w;
    record.height = b.h;
    m_index.update(record.handle, b);
    return true;
}

WorkspaceNode* WorkspaceModel::find(const std::string& nodeId) const {
    const auto it = m_nodes.find(nodeId);
    return it == m_nodes.end() ? nullptr : it->second.node.get();
}

WorkspaceNode* WorkspaceModel::findRecursive(const std::string& nodeId) const {
    const auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end()) {
        return it->second.node.get();
    }

    for (const auto& [_, record] : m_nodes) {
        if (const auto* stack = dynamic_cast<const CardStackNode*>(record.node.get())) {
            auto* found = stack->findChildRecursive(nodeId);
            if (found) {
                return found;
            }
        }
    }
    return nullptr;
}

CardStackNode* WorkspaceModel::stackContainingNode(const std::string& childId) const {
    for (const auto& [_, record] : m_nodes) {
        if (auto* stack = dynamic_cast<CardStackNode*>(record.node.get())) {
            if (stack->containsChild(childId)) {
                return stack;
            }
        }
    }
    return nullptr;
}

Point WorkspaceModel::positionOf(const std::string& nodeId) const {
    const Rectangle bounds = boundsOf(nodeId);
    return {bounds.x, bounds.y};
}

Rectangle WorkspaceModel::boundsOf(const std::string& nodeId) const {
    const auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end()) {
        const Record& record = it->second;
        return {record.x, record.y, record.width, record.height};
    }

    // Node might be a child inside a CardStackNode
    for (const auto& [_, record] : m_nodes) {
        if (const auto* stack = dynamic_cast<const CardStackNode*>(record.node.get())) {
            if (const auto* child = stack->findChildRecursive(nodeId)) {
                if (stack->isCollapsed()) {
                    // When stack is collapsed, return the composite visible stack bounds
                    return stack->bounds();
                } else {
                    // When stack is expanded, return the child card's active bounds
                    return child->bounds();
                }
            }
        }
    }

    return {};
}

bool WorkspaceModel::dissolveStackIfSingleChild(const std::string& stackId,
                                                std::string* extractedChildId) {
    const auto it = m_nodes.find(stackId);
    if (it == m_nodes.end())
        return false;

    auto* stack = dynamic_cast<CardStackNode*>(it->second.node.get());
    if (!stack)
        return false;

    if (stack->childCount() > 1) {
        return false;
    }

    if (stack->childCount() == 1) {
        auto child = std::move(
            const_cast<std::vector<std::unique_ptr<WorkspaceNode>>&>(stack->children())[0]);
        const std::string childId = child->id();
        if (extractedChildId) {
            *extractedChildId = childId;
        }

        // Remove the stack node
        remove(stackId);

        // Insert the extracted child node as a top-level node
        insert(std::move(child));
        return true;
    } else {
        // 0 children: simply remove empty stack
        remove(stackId);
        return true;
    }
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

Rectangle WorkspaceModel::globalBounds() const {
    if (m_nodes.empty()) {
        return {0.0, 0.0, 0.0, 0.0};
    }

    auto it = m_nodes.begin();
    double minX = it->second.x;
    double minY = it->second.y;
    double maxX = it->second.x + it->second.width;
    double maxY = it->second.y + it->second.height;
    ++it;

    for (; it != m_nodes.end(); ++it) {
        if (it->second.x < minX)
            minX = it->second.x;
        if (it->second.y < minY)
            minY = it->second.y;
        if (it->second.x + it->second.width > maxX)
            maxX = it->second.x + it->second.width;
        if (it->second.y + it->second.height > maxY)
            maxY = it->second.y + it->second.height;
    }

    return {minX, minY, maxX - minX, maxY - minY};
}

std::vector<std::string> WorkspaceModel::allNodeIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_nodes.size());
    for (const auto& [id, _] : m_nodes) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace FluidCore
