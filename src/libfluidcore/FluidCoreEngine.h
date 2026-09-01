#pragma once

#include "FluidCoreAPI.h"

#include "graph/GraphTopology.h"
#include "squeeze/SqueezeEngine.h"
#include "storage/ProjectStore.h"
#include "workspace/CardStackNode.h"
#include "workspace/PhysicsSolver.h"
#include "workspace/WorkspaceModel.h"

#include <memory>
#include <string>
#include <vector>

namespace FluidCore {

// Concrete FluidCoreAPI facade over the engine modules.
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

    WorkspaceModel& workspaceModel() { return m_model; }
    const WorkspaceModel& workspaceModel() const { return m_model; }

    ProjectStore& projectStore() { return m_store; }
    const ProjectStore& projectStore() const { return m_store; }

    // Spatial scene graph — live slice backed by WorkspaceModel + RTreeIndex.
    std::string insertNode(std::unique_ptr<WorkspaceNode> node) override;
    void updateNodePosition(const std::string& nodeId, double x, double y) override;
    void removeNode(const std::string& nodeId) override;
    std::vector<WorkspaceNode*> queryVisibleNodes(const Rectangle& viewportBounds) const override;

    // Pure geometry exposure (ADR-0001): values only, nothing renderable.
    Rectangle getNodeBounds(const std::string& nodeId) const override;
    Point getNodePosition(const std::string& nodeId) const override;
    Rectangle getWorkspaceBounds() const override;

    // Spatial Snapping, Stacking & Physics (TASK-4.2)
    SnapResult solveSnap(const Rectangle& dragBounds, double snapThreshold = 16.0,
                         const std::string& ignoreId = "") const override;
    std::string mergeNodesIntoStack(const std::string& sourceNodeId,
                                    const std::string& targetNodeId) override;
    std::string extractChildFromStack(const std::string& stackId, const std::string& childId,
                                      const Point& dropPos) override;
    bool setStackCollapsed(const std::string& stackId, bool collapsed) override;
    bool toggleStackCollapsed(const std::string& stackId) override;
    bool isStackNode(const std::string& nodeId) const override;
    bool isStackCollapsed(const std::string& stackId) const override;
    std::vector<std::string> getStackChildren(const std::string& stackId) const override;
    bool setStackTitle(const std::string& stackId, const std::string& title) override;
    std::string getStackTitle(const std::string& stackId) const override;

    // Relational graph & ink links — backed by GraphTopology.
    std::string createInkLink(const std::string& sourceNodeId, const std::string& targetNodeId,
                              const Color& color) override;
    BezierSpline getEdgeGeometry(const std::string& edgeId) const override;
    std::vector<std::string> getConnectedEdges(const std::string& nodeId) const override;
    std::vector<std::string> getAllEdges() const override;
    bool removeEdge(const std::string& edgeId) override;

    GraphTopology& graphTopology() { return m_graph; }
    const GraphTopology& graphTopology() const { return m_graph; }

    // Workspace Markdown Outline Export (TASK-4.4)
    WorkspaceExportResult
    exportWorkspaceMarkdown(const WorkspaceExportOptions& options = {}) const override;
    bool exportWorkspaceMarkdownToFile(const std::string& filePath,
                                       const WorkspaceExportOptions& options = {}) const override;

    // Persistence & search (TASK-5.1)
    void openProject(const std::string& ltprojDirectoryPath) override;
    void saveProject() override;
    std::vector<SearchResult> executeSearch(const std::string& query) const override;
    std::vector<WorkspaceMatch> searchWorkspace(const std::string& query,
                                                bool caseSensitive = false) const override;

  private:
    WorkspaceModel m_model;
    SqueezeEngine m_squeezeEngine;
    GraphTopology m_graph;
    ProjectStore m_store;
};

} // namespace FluidCore
