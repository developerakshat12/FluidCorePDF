#include "undo/WorkspaceCommands.h"

#include <utility>

namespace FluidCore {

// --- MoveNodeCommand ---

MoveNodeCommand::MoveNodeCommand(WorkspaceModel& model, std::string nodeId, Point oldPos,
                                 Point newPos)
    : m_model(model), m_nodeId(std::move(nodeId)), m_oldPos(oldPos), m_newPos(newPos) {}

bool MoveNodeCommand::execute() {
    return m_model.move(m_nodeId, m_newPos.x, m_newPos.y);
}

bool MoveNodeCommand::undo() {
    return m_model.move(m_nodeId, m_oldPos.x, m_oldPos.y);
}

bool MoveNodeCommand::redo() {
    return m_model.move(m_nodeId, m_newPos.x, m_newPos.y);
}

std::size_t MoveNodeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_nodeId.capacity();
}

// --- InsertNodeCommand ---

InsertNodeCommand::InsertNodeCommand(WorkspaceModel& model, std::unique_ptr<WorkspaceNode> node)
    : m_model(model) {
    if (node) {
        m_nodeId = node->id();
        m_nodeTemplate = node->clone();
        if (!m_nodeTemplate) {
            m_nodeTemplate = std::move(node);
        }
    }
}

bool InsertNodeCommand::execute() {
    if (m_model.find(m_nodeId)) {
        return true;
    }
    if (!m_nodeTemplate) {
        return false;
    }
    auto clone = m_nodeTemplate->clone();
    if (!clone) {
        // Fallback: if node doesn't support clone, move the template directly
        return !m_model.insert(std::move(m_nodeTemplate)).empty();
    }
    return !m_model.insert(std::move(clone)).empty();
}

bool InsertNodeCommand::undo() {
    auto* node = m_model.find(m_nodeId);
    if (node && !m_nodeTemplate) {
        m_nodeTemplate = node->clone();
    }
    return m_model.remove(m_nodeId);
}

bool InsertNodeCommand::redo() {
    return execute();
}

std::size_t InsertNodeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_nodeId.capacity() + 256;
}

// --- RemoveNodeCommand ---

RemoveNodeCommand::RemoveNodeCommand(WorkspaceModel& model, std::string nodeId)
    : m_model(model), m_nodeId(std::move(nodeId)) {}

bool RemoveNodeCommand::execute() {
    auto* node = m_model.find(m_nodeId);
    if (node) {
        m_savedNode = node->clone();
    }
    return m_model.remove(m_nodeId);
}

bool RemoveNodeCommand::undo() {
    if (!m_savedNode) {
        return false;
    }
    auto clone = m_savedNode->clone();
    if (!clone) {
        return !m_model.insert(std::move(m_savedNode)).empty();
    }
    return !m_model.insert(std::move(clone)).empty();
}

bool RemoveNodeCommand::redo() {
    return execute();
}

std::size_t RemoveNodeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_nodeId.capacity() + (m_savedNode ? 256 : 0);
}

} // namespace FluidCore
