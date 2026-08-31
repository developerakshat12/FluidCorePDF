#include "FluidCoreEngine.h"

#include "workspace/ExcerptCardNode.h"

#include <utility>

namespace FluidCore {

FluidCoreEngine::FluidCoreEngine(std::string projectId) : m_model(std::move(projectId)) {}

void FluidCoreEngine::registerDocumentGeometry(const std::string& docId,
                                               const std::vector<PageGeometry>& pages) {
    m_squeezeEngine.registerDocumentGeometry(docId, pages);
}

CoordinateTransformResult FluidCoreEngine::mapDocumentYToScreen(double docY,
                                                                const std::string& docId) const {
    return m_squeezeEngine.mapDocumentYToScreen(docY, docId);
}

CoordinateTransformResult FluidCoreEngine::mapScreenYToDocument(double screenY,
                                                                const std::string& docId) const {
    return m_squeezeEngine.mapScreenYToDocument(screenY, docId);
}

void FluidCoreEngine::setSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                                       double alpha) {
    m_squeezeEngine.setSqueezeRegion(docId, yStart, yEnd, alpha);
}

void FluidCoreEngine::setSqueezeRegionWithId(const std::string& docId, const std::string& regionId,
                                             double yStart, double yEnd, double alpha) {
    m_squeezeEngine.setSqueezeRegionWithId(docId, regionId, yStart, yEnd, alpha);
}

void FluidCoreEngine::removeSqueezeRegion(const std::string& docId, const std::string& regionId) {
    m_squeezeEngine.removeSqueezeRegion(docId, regionId);
}

void FluidCoreEngine::resetSqueeze(const std::string& docId) {
    m_squeezeEngine.resetSqueeze(docId);
}

std::vector<SqueezeSegment> FluidCoreEngine::getSqueezeSegments(const std::string& docId) const {
    return m_squeezeEngine.getSegments(docId);
}

double FluidCoreEngine::getTotalSqueezedHeight(const std::string& docId) const {
    return m_squeezeEngine.totalSqueezedHeight(docId);
}

std::string FluidCoreEngine::insertNode(std::unique_ptr<WorkspaceNode> node) {
    return m_model.insert(std::move(node));
}

void FluidCoreEngine::updateNodePosition(const std::string& nodeId, double x, double y) {
    m_model.move(nodeId, x, y);
}

void FluidCoreEngine::removeNode(const std::string& nodeId) {
    m_graph.removeEdgesForNode(nodeId);
    m_model.remove(nodeId);
}

std::vector<WorkspaceNode*>
FluidCoreEngine::queryVisibleNodes(const Rectangle& viewportBounds) const {
    return m_model.visibleIn(viewportBounds);
}

Rectangle FluidCoreEngine::getNodeBounds(const std::string& nodeId) const {
    return m_model.boundsOf(nodeId);
}

Point FluidCoreEngine::getNodePosition(const std::string& nodeId) const {
    return m_model.positionOf(nodeId);
}

Rectangle FluidCoreEngine::getWorkspaceBounds() const {
    return m_model.globalBounds();
}

SnapResult FluidCoreEngine::solveSnap(const Rectangle& dragBounds, double snapThreshold,
                                      const std::string& ignoreId) const {
    std::vector<CandidateTarget> candidates;
    for (const auto& id : m_model.allNodeIds()) {
        if (!ignoreId.empty() && id == ignoreId) {
            continue;
        }
        auto* node = m_model.find(id);
        if (!node)
            continue;
        const bool isStack = (dynamic_cast<const CardStackNode*>(node) != nullptr);
        candidates.push_back(CandidateTarget{id, node->bounds(), isStack});
    }

    return PhysicsSolver::solveSnap(dragBounds, candidates, snapThreshold,
                                    PhysicsSolver::kDefaultStackOverlapThreshold, ignoreId);
}

std::string FluidCoreEngine::mergeNodesIntoStack(const std::string& sourceNodeId,
                                                 const std::string& targetNodeId) {
    auto* srcNode = m_model.find(sourceNodeId);
    auto* dstNode = m_model.find(targetNodeId);
    if (!srcNode || !dstNode) {
        return {};
    }

    auto clonedSrc = srcNode->clone();
    auto clonedDst = dstNode->clone();

    if (auto* dstStack = dynamic_cast<CardStackNode*>(dstNode)) {
        m_model.remove(sourceNodeId);
        dstStack->addChild(std::move(clonedSrc));
        m_model.updateBounds(targetNodeId);
        return targetNodeId;
    } else {
        static std::size_t s_stackCounter = 1;
        std::string stackId = "stack-" + std::to_string(s_stackCounter++);

        auto newStack = std::make_unique<CardStackNode>(stackId, dstNode->bounds());
        newStack->addChild(std::move(clonedDst));
        newStack->addChild(std::move(clonedSrc));

        m_model.remove(sourceNodeId);
        m_model.remove(targetNodeId);
        m_model.insert(std::move(newStack));
        return stackId;
    }
}

std::string FluidCoreEngine::extractChildFromStack(const std::string& stackId,
                                                   const std::string& childId,
                                                   const Point& dropPos) {
    auto* node = m_model.find(stackId);
    auto* stack = dynamic_cast<CardStackNode*>(node);
    if (!stack) {
        return {};
    }

    auto child = stack->removeChild(childId);
    if (!child) {
        return {};
    }

    if (auto* excerpt = dynamic_cast<ExcerptCardNode*>(child.get())) {
        excerpt->setPosition(dropPos.x, dropPos.y);
    } else if (auto* childStack = dynamic_cast<CardStackNode*>(child.get())) {
        childStack->setPosition(dropPos.x, dropPos.y);
    }

    const std::string extractedId = child->id();
    m_model.insert(std::move(child));

    std::string dissolvedRemainingId;
    if (!m_model.dissolveStackIfSingleChild(stackId, &dissolvedRemainingId)) {
        m_model.updateBounds(stackId);
    }
    return extractedId;
}

bool FluidCoreEngine::setStackCollapsed(const std::string& stackId, bool collapsed) {
    auto* stack = dynamic_cast<CardStackNode*>(m_model.find(stackId));
    if (!stack) {
        return false;
    }
    stack->setCollapsed(collapsed);
    m_model.updateBounds(stackId);
    return true;
}

bool FluidCoreEngine::toggleStackCollapsed(const std::string& stackId) {
    auto* stack = dynamic_cast<CardStackNode*>(m_model.find(stackId));
    if (!stack) {
        return false;
    }
    stack->toggleCollapsed();
    m_model.updateBounds(stackId);
    return true;
}

bool FluidCoreEngine::isStackNode(const std::string& nodeId) const {
    return dynamic_cast<const CardStackNode*>(m_model.find(nodeId)) != nullptr;
}

bool FluidCoreEngine::isStackCollapsed(const std::string& stackId) const {
    const auto* stack = dynamic_cast<const CardStackNode*>(m_model.find(stackId));
    return stack ? stack->isCollapsed() : false;
}

std::vector<std::string> FluidCoreEngine::getStackChildren(const std::string& stackId) const {
    const auto* stack = dynamic_cast<const CardStackNode*>(m_model.find(stackId));
    if (!stack) {
        return {};
    }
    std::vector<std::string> ids;
    ids.reserve(stack->childCount());
    for (const auto& child : stack->children()) {
        if (child) {
            ids.push_back(child->id());
        }
    }
    return ids;
}

bool FluidCoreEngine::setStackTitle(const std::string& stackId, const std::string& title) {
    auto* node = m_model.find(stackId);
    if (auto* stack = dynamic_cast<CardStackNode*>(node)) {
        stack->setTitle(title);
        return true;
    }
    return false;
}

std::string FluidCoreEngine::getStackTitle(const std::string& stackId) const {
    const auto* node = m_model.find(stackId);
    if (const auto* stack = dynamic_cast<const CardStackNode*>(node)) {
        return stack->title();
    }
    return "";
}

std::string FluidCoreEngine::createInkLink(const std::string& sourceNodeId,
                                           const std::string& targetNodeId, const Color& color) {
    return m_graph.addEdge(sourceNodeId, targetNodeId, color);
}

BezierSpline FluidCoreEngine::getEdgeGeometry(const std::string& edgeId) const {
    auto edgeOpt = m_graph.findEdge(edgeId);
    if (!edgeOpt.has_value()) {
        return {};
    }
    const Rectangle srcBounds = m_model.boundsOf(edgeOpt->sourceNodeId);
    const Rectangle dstBounds = m_model.boundsOf(edgeOpt->targetNodeId);
    return m_graph.computeEdgeSpline(edgeId, srcBounds, dstBounds);
}

std::vector<std::string> FluidCoreEngine::getConnectedEdges(const std::string& nodeId) const {
    return m_graph.connectedEdgeIds(nodeId);
}

std::vector<std::string> FluidCoreEngine::getAllEdges() const {
    return m_graph.allEdgeIds();
}

bool FluidCoreEngine::removeEdge(const std::string& edgeId) {
    return m_graph.removeEdge(edgeId);
}

void FluidCoreEngine::openProject(const std::string&) {}

void FluidCoreEngine::saveProject() {}

std::vector<SearchResult> FluidCoreEngine::executeSearch(const std::string&) const {
    return {};
}

} // namespace FluidCore
