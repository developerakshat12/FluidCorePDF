#include "FluidCoreEngine.h"

#include <utility>

namespace FluidCore {

FluidCoreEngine::FluidCoreEngine(std::string projectId) : m_model(std::move(projectId)) {}

void FluidCoreEngine::registerDocumentGeometry(const std::string&,
                                               const std::vector<PageGeometry>&) {
    // TODO(M2): store per-document page stacks for the squeeze mapper.
}

CoordinateTransformResult FluidCoreEngine::mapDocumentYToScreen(double, const std::string&) const {
    return {};
}

CoordinateTransformResult FluidCoreEngine::mapScreenYToDocument(double, const std::string&) const {
    return {};
}

void FluidCoreEngine::setSqueezeRegion(const std::string&, double, double, double) {}

void FluidCoreEngine::resetSqueeze(const std::string&) {}

std::string FluidCoreEngine::insertNode(std::unique_ptr<WorkspaceNode> node) {
    return m_model.insert(std::move(node));
}

void FluidCoreEngine::updateNodePosition(const std::string& nodeId, double x, double y) {
    m_model.move(nodeId, x, y);
}

void FluidCoreEngine::removeNode(const std::string& nodeId) {
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

std::string FluidCoreEngine::createInkLink(const std::string&, const std::string&, const Color&) {
    // TODO(M4): route through GraphTopology and return the edge id.
    return {};
}

BezierSpline FluidCoreEngine::getEdgeGeometry(const std::string&) const {
    return {};
}

std::vector<std::string> FluidCoreEngine::getConnectedEdges(const std::string&) const {
    return {};
}

void FluidCoreEngine::openProject(const std::string&) {}

void FluidCoreEngine::saveProject() {}

std::vector<SearchResult> FluidCoreEngine::executeSearch(const std::string&) const {
    return {};
}

} // namespace FluidCore
