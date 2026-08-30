#pragma once

#include "FluidCoreAPI.h"

#include "squeeze/SqueezeEngine.h"
#include "workspace/WorkspaceModel.h"

#include <memory>
#include <string>
#include <vector>

namespace FluidCore {

// Concrete FluidCoreAPI facade over the engine modules. Wave-1 slice: the
// spatial scene-graph and squeeze engine methods are live; edge routing (M4) and
// persistence/search (M5) stay signature-level no-ops at their delegate points.
class FluidCoreEngine final : public FluidCoreAPI {
  public:
    explicit FluidCoreEngine(std::string projectId);

    // Document geometry & squeeze layout — delegated to SqueezeEngine.
    void registerDocumentGeometry(const std::string& docId,
                                  const std::vector<PageGeometry>& pages) override;
    CoordinateTransformResult mapDocumentYToScreen(double docY,
                                                   const std::string& docId) const override;
    CoordinateTransformResult mapScreenYToDocument(double screenY,
                                                   const std::string& docId) const override;
    void setSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                          double alpha) override;
    void setSqueezeRegionWithId(const std::string& docId, const std::string& regionId,
                                double yStart, double yEnd, double alpha) override;
    void removeSqueezeRegion(const std::string& docId, const std::string& regionId) override;
    void resetSqueeze(const std::string& docId) override;
    std::vector<SqueezeSegment> getSqueezeSegments(const std::string& docId) const override;
    double getTotalSqueezedHeight(const std::string& docId) const override;

    SqueezeEngine& squeezeEngine() { return m_squeezeEngine; }
    const SqueezeEngine& squeezeEngine() const { return m_squeezeEngine; }

    // Spatial scene graph — live slice backed by WorkspaceModel + RTreeIndex.
    std::string insertNode(std::unique_ptr<WorkspaceNode> node) override;
    void updateNodePosition(const std::string& nodeId, double x, double y) override;
    void removeNode(const std::string& nodeId) override;
    std::vector<WorkspaceNode*> queryVisibleNodes(const Rectangle& viewportBounds) const override;

    // Pure geometry exposure (ADR-0001): values only, nothing renderable.
    Rectangle getNodeBounds(const std::string& nodeId) const override;
    Point getNodePosition(const std::string& nodeId) const override;

    // Relational graph & ink links — TODO(M4): delegate to GraphTopology.
    std::string createInkLink(const std::string& sourceNodeId, const std::string& targetNodeId,
                              const Color& color) override;
    BezierSpline getEdgeGeometry(const std::string& edgeId) const override;
    std::vector<std::string> getConnectedEdges(const std::string& nodeId) const override;

    // Persistence & search — TODO(M5): ProjectStore once docs/specs/ltspec.md locks schema.
    void openProject(const std::string& ltprojDirectoryPath) override;
    void saveProject() override;
    std::vector<SearchResult> executeSearch(const std::string& query) const override;

  private:
    WorkspaceModel m_model;
    SqueezeEngine m_squeezeEngine;
};

} // namespace FluidCore
