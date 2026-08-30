#include "FluidCoreEngine.h"

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
