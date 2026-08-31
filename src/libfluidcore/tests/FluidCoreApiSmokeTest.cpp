// Headless smoke test: the public header compiles, every pure-virtual signature is
// implementable without GTK/Poppler types, and module stubs link. No framework deps.

#include "FluidCoreAPI.h"

#include "graph/GraphTopology.h"
#include "squeeze/SqueezeEngine.h"
#include "storage/ProjectStore.h"
#include "workspace/WorkspaceModel.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace FluidCore;

class StubApi final : public FluidCoreAPI {
  public:
    void registerDocumentGeometry(const std::string&, const std::vector<PageGeometry>&) override {}
    CoordinateTransformResult mapDocumentYToScreen(double, const std::string&) const override {
        return {};
    }
    CoordinateTransformResult mapScreenYToDocument(double, const std::string&) const override {
        return {};
    }
    void setSqueezeRegion(const std::string&, double, double, double) override {}
    void setSqueezeRegionWithId(const std::string&, const std::string&, double, double,
                                double) override {}
    void removeSqueezeRegion(const std::string&, const std::string&) override {}
    void resetSqueeze(const std::string&) override {}
    std::vector<SqueezeSegment> getSqueezeSegments(const std::string&) const override { return {}; }
    double getTotalSqueezedHeight(const std::string&) const override { return 0.0; }

    std::string insertNode(std::unique_ptr<WorkspaceNode>) override { return {}; }
    void updateNodePosition(const std::string&, double, double) override {}
    void removeNode(const std::string&) override {}
    std::vector<WorkspaceNode*> queryVisibleNodes(const Rectangle&) const override { return {}; }

    Rectangle getNodeBounds(const std::string&) const override { return {}; }
    Point getNodePosition(const std::string&) const override { return {}; }
    Rectangle getWorkspaceBounds() const override { return {}; }

    std::string createInkLink(const std::string&, const std::string&, const Color&) override {
        return {};
    }
    BezierSpline getEdgeGeometry(const std::string&) const override { return {}; }
    std::vector<std::string> getConnectedEdges(const std::string&) const override { return {}; }
    std::vector<std::string> getAllEdges() const override { return {}; }
    bool removeEdge(const std::string&) override { return false; }

    SnapResult solveSnap(const Rectangle&, double, const std::string&) const override { return {}; }
    std::string mergeNodesIntoStack(const std::string&, const std::string&) override { return {}; }
    std::string extractChildFromStack(const std::string&, const std::string&,
                                      const Point&) override {
        return {};
    }
    bool setStackCollapsed(const std::string&, bool) override { return false; }
    bool toggleStackCollapsed(const std::string&) override { return false; }
    bool isStackNode(const std::string&) const override { return false; }
    bool isStackCollapsed(const std::string&) const override { return false; }
    std::vector<std::string> getStackChildren(const std::string&) const override { return {}; }
    bool setStackTitle(const std::string&, const std::string&) override { return false; }
    std::string getStackTitle(const std::string&) const override { return ""; }

    // Signature-only until M5; must remain implementable as a no-op.
    void openProject(const std::string&) override {}
    void saveProject() override {}
    std::vector<SearchResult> executeSearch(const std::string&) const override { return {}; }
};

class StubNode final : public WorkspaceNode {
  public:
    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return {}; }

  private:
    std::string m_id{"stub-node"};
};

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    int failures = 0;

    failures += check(std::strcmp(kFluidCoreVersion, "0.0.0-bootstrap") == 0, "version constant");

    StubApi api;
    api.registerDocumentGeometry("doc-1", {{0, 612.0, 792.0, 0.0}});
    failures +=
        check(api.mapDocumentYToScreen(100.0, "doc-1").alpha == 1.0, "default alpha is unsqueezed");

    auto node = std::make_unique<StubNode>();
    failures += check(api.insertNode(std::move(node)).empty() || true, "insertNode callable");
    failures += check(api.getEdgeGeometry("e").controlPoints.empty(), "spline stub empty");
    failures += check(api.executeSearch("q").empty(), "search stub empty");

    SqueezeEngine squeeze;
    WorkspaceModel model("p-1");
    GraphTopology graph;
    ProjectStore store;
    failures += check(model.projectId() == "p-1", "workspace model identity");

    if (failures == 0) {
        std::cout << "FluidCoreApiSmokeTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
