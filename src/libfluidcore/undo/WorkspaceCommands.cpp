#include "undo/WorkspaceCommands.h"

#include "workspace/CanvasStrokeNode.h"
#include "workspace/ExcerptCardNode.h"

#include <atomic>
#include <chrono>
#include <string>
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
        if (m_nodeTemplate) {
            static std::atomic<uint64_t> s_fallbackSeq{0};
            auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
            std::string prefix = m_nodeId.substr(0, m_nodeId.find('-'));
            if (prefix.empty()) {
                prefix = "node";
            }
            std::string newId;
            do {
                newId =
                    prefix + "-" + std::to_string(nowUs) + "-" + std::to_string(++s_fallbackSeq);
            } while (m_model.find(newId) != nullptr);

            if (auto* strokeNode = dynamic_cast<CanvasStrokeNode*>(m_nodeTemplate.get())) {
                auto stroke = strokeNode->stroke();
                stroke.id = newId;
                strokeNode->setStroke(std::move(stroke));
            }
            m_nodeId = newId;
        } else {
            return true;
        }
    }
    if (!m_nodeTemplate) {
        return false;
    }
    auto clone = m_nodeTemplate->clone();
    if (!clone) {
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

// --- StackMergeCommand ---

StackMergeCommand::StackMergeCommand(WorkspaceModel& model, std::string sourceNodeId,
                                     std::string targetNodeId, std::string stackId)
    : m_model(model), m_sourceId(std::move(sourceNodeId)), m_targetId(std::move(targetNodeId)),
      m_stackId(std::move(stackId)) {}

bool StackMergeCommand::execute() {
    auto* srcNode = m_model.find(m_sourceId);
    auto* dstNode = m_model.find(m_targetId);
    if (!srcNode || !dstNode) {
        return false;
    }

    m_savedSourceNode = srcNode->clone();
    m_savedTargetNode = dstNode->clone();

    if (auto* dstStack = dynamic_cast<CardStackNode*>(dstNode)) {
        m_targetWasStack = true;
        m_stackId = m_targetId;
        m_model.remove(m_sourceId);
        dstStack->addChild(m_savedSourceNode->clone());
        m_model.updateBounds(m_stackId);
        return true;
    } else {
        m_targetWasStack = false;
        if (m_stackId.empty()) {
            static std::atomic<uint64_t> s_stackSeq{0};
            auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
            do {
                m_stackId = "stack-" + std::to_string(nowUs) + "-" + std::to_string(++s_stackSeq);
            } while (m_model.find(m_stackId) != nullptr);
        }

        auto newStack = std::make_unique<CardStackNode>(m_stackId, dstNode->bounds());
        newStack->addChild(m_savedTargetNode->clone());
        newStack->addChild(m_savedSourceNode->clone());

        m_model.remove(m_sourceId);
        m_model.remove(m_targetId);
        m_model.insert(std::move(newStack));
        return true;
    }
}

bool StackMergeCommand::undo() {
    if (m_targetWasStack) {
        auto* stack = dynamic_cast<CardStackNode*>(m_model.find(m_stackId));
        if (!stack || !m_savedSourceNode) {
            return false;
        }
        stack->removeChild(m_sourceId);
        m_model.insert(m_savedSourceNode->clone());
        m_model.updateBounds(m_stackId);
        return true;
    } else {
        m_model.remove(m_stackId);
        if (m_savedTargetNode) {
            m_model.insert(m_savedTargetNode->clone());
        }
        if (m_savedSourceNode) {
            m_model.insert(m_savedSourceNode->clone());
        }
        return true;
    }
}

bool StackMergeCommand::redo() {
    return execute();
}

std::size_t StackMergeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_sourceId.capacity() + m_targetId.capacity() + m_stackId.capacity() +
           512;
}

// --- ExtractChildCommand ---

ExtractChildCommand::ExtractChildCommand(WorkspaceModel& model, std::string stackId,
                                         std::string childId, Point dropPos)
    : m_model(model), m_stackId(std::move(stackId)), m_childId(std::move(childId)),
      m_dropPos(dropPos) {}

bool ExtractChildCommand::execute() {
    auto* stack = dynamic_cast<CardStackNode*>(m_model.find(m_stackId));
    if (!stack) {
        return false;
    }

    m_savedStack = stack->clone();
    auto child = stack->removeChild(m_childId);
    if (!child) {
        return false;
    }

    m_savedChild = child->clone();

    if (auto* excerpt = dynamic_cast<ExcerptCardNode*>(child.get())) {
        excerpt->setPosition(m_dropPos.x, m_dropPos.y);
    } else if (auto* childStack = dynamic_cast<CardStackNode*>(child.get())) {
        childStack->setPosition(m_dropPos.x, m_dropPos.y);
    }

    m_model.insert(std::move(child));

    std::string dissolvedChildId;
    m_stackWasDissolved = m_model.dissolveStackIfSingleChild(m_stackId, &dissolvedChildId);
    if (!m_stackWasDissolved) {
        m_model.updateBounds(m_stackId);
    }
    return true;
}

bool ExtractChildCommand::undo() {
    m_model.remove(m_childId);

    if (m_stackWasDissolved) {
        if (m_savedStack) {
            const auto* savedStackPtr = dynamic_cast<const CardStackNode*>(m_savedStack.get());
            if (savedStackPtr) {
                for (const auto& c : savedStackPtr->children()) {
                    m_model.remove(c->id());
                }
            }
            m_model.insert(m_savedStack->clone());
        }
        return true;
    } else {
        auto* stack = dynamic_cast<CardStackNode*>(m_model.find(m_stackId));
        if (stack && m_savedChild) {
            stack->addChild(m_savedChild->clone());
            m_model.updateBounds(m_stackId);
            return true;
        }
        return false;
    }
}

bool ExtractChildCommand::redo() {
    return execute();
}

std::size_t ExtractChildCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_stackId.capacity() + m_childId.capacity() + 512;
}

// --- ToggleStackCollapseCommand ---

ToggleStackCollapseCommand::ToggleStackCollapseCommand(WorkspaceModel& model, std::string stackId,
                                                       bool collapsed)
    : m_model(model), m_stackId(std::move(stackId)), m_newCollapsed(collapsed) {}

bool ToggleStackCollapseCommand::execute() {
    auto* stack = dynamic_cast<CardStackNode*>(m_model.find(m_stackId));
    if (!stack) {
        return false;
    }

    m_oldCollapsed = stack->isCollapsed();
    stack->setCollapsed(m_newCollapsed);
    m_model.updateBounds(m_stackId);
    return true;
}

bool ToggleStackCollapseCommand::undo() {
    auto* stack = dynamic_cast<CardStackNode*>(m_model.find(m_stackId));
    if (!stack) {
        return false;
    }

    stack->setCollapsed(m_oldCollapsed);
    m_model.updateBounds(m_stackId);
    return true;
}

bool ToggleStackCollapseCommand::redo() {
    return execute();
}

std::size_t ToggleStackCollapseCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_stackId.capacity();
}

// --- CreateInkLinkCommand ---

CreateInkLinkCommand::CreateInkLinkCommand(GraphTopology& graph, std::string sourceId,
                                           std::string targetId, Color color, double strokeWidth,
                                           ArrowStyle arrowStyle, std::string label)
    : m_graph(graph) {
    m_edge.sourceNodeId = std::move(sourceId);
    m_edge.targetNodeId = std::move(targetId);
    m_edge.color = color;
    m_edge.strokeWidth = strokeWidth;
    m_edge.arrowStyle = arrowStyle;
    m_edge.label = std::move(label);
}

CreateInkLinkCommand::CreateInkLinkCommand(GraphTopology& graph, GraphEdge edge)
    : m_graph(graph), m_edge(std::move(edge)), m_edgeId(m_edge.id) {}

bool CreateInkLinkCommand::execute() {
    if (!m_edgeId.empty() && m_graph.findEdge(m_edgeId).has_value()) {
        return true;
    }
    m_edgeId = m_graph.addEdge(m_edge);
    m_edge.id = m_edgeId;
    return !m_edgeId.empty();
}

bool CreateInkLinkCommand::undo() {
    if (m_edgeId.empty()) {
        return false;
    }
    return m_graph.removeEdge(m_edgeId);
}

bool CreateInkLinkCommand::redo() {
    return execute();
}

std::size_t CreateInkLinkCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_edgeId.capacity() + m_edge.sourceNodeId.capacity() +
           m_edge.targetNodeId.capacity() + m_edge.label.capacity();
}

// --- RemoveEdgeCommand ---

RemoveEdgeCommand::RemoveEdgeCommand(GraphTopology& graph, std::string edgeId)
    : m_graph(graph), m_edgeId(std::move(edgeId)) {}

bool RemoveEdgeCommand::execute() {
    auto edgeOpt = m_graph.findEdge(m_edgeId);
    if (edgeOpt.has_value()) {
        m_savedEdge = edgeOpt;
    }
    return m_graph.removeEdge(m_edgeId);
}

bool RemoveEdgeCommand::undo() {
    if (!m_savedEdge.has_value()) {
        return false;
    }
    std::string addedId = m_graph.addEdge(*m_savedEdge);
    if (!addedId.empty()) {
        m_edgeId = addedId;
        return true;
    }
    return false;
}

bool RemoveEdgeCommand::redo() {
    return execute();
}

std::size_t RemoveEdgeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_edgeId.capacity() + 128;
}

} // namespace FluidCore
