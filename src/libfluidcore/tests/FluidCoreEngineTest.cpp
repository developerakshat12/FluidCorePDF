// Headless unit tests for the FluidCoreEngine facade (mirrors FluidCoreEngine.cpp).

#include "FluidCoreEngine.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace FluidCore;

class RectNode final : public WorkspaceNode {
  public:
    RectNode(std::string id, Rectangle bounds) : m_id(std::move(id)), m_bounds(bounds) {}
    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return m_bounds; }

  private:
    std::string m_id;
    Rectangle m_bounds;
};

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

bool close(double a, double b) {
    const double eps = 1e-9;
    return a - eps <= b && b <= a + eps;
}

} // namespace

int main() {
    int failures = 0;

    FluidCoreEngine engine("proj-1");

    // Exercise the live spatial slice through the abstract API boundary, exactly
    // as src/app will.
    FluidCoreAPI& api = engine;

    const std::string id =
        api.insertNode(std::make_unique<RectNode>("node-1", Rectangle{100.0, 100.0, 40.0, 30.0}));
    failures += check(id == "node-1", "insertNode returns the node identity");

    const Point pos = api.getNodePosition(id);
    failures += check(close(pos.x, 100.0) && close(pos.y, 100.0), "getNodePosition round-trips");
    const Rectangle bounds = api.getNodeBounds(id);
    failures += check(close(bounds.w, 40.0) && close(bounds.h, 30.0),
                      "getNodeBounds preserves the registered size");

    failures += check(api.queryVisibleNodes({90.0, 90.0, 60.0, 50.0}).size() == 1,
                      "viewport containing the node sees it");
    failures += check(api.queryVisibleNodes({300.0, 300.0, 50.0, 50.0}).empty(),
                      "distant viewport sees nothing");
    failures +=
        check(api.getNodePosition("missing").x == 0.0 && api.getNodeBounds("missing").w == 0.0,
              "unknown ids return zero geometry");

    api.updateNodePosition(id, 400.0, 400.0);
    failures += check(api.queryVisibleNodes({390.0, 390.0, 60.0, 60.0}).size() == 1 &&
                          api.queryVisibleNodes({90.0, 90.0, 60.0, 50.0}).empty(),
                      "updateNodePosition relocates the node in the index");

    api.removeNode(id);
    failures += check(api.queryVisibleNodes({0.0, 0.0, 1000.0, 1000.0}).empty(),
                      "removeNode empties the scene");
    api.removeNode("missing"); // must not crash

    // Squeeze mapping delegates to live SqueezeEngine
    api.registerDocumentGeometry("doc-1", {{0, 612.0, 792.0, 0.0}});
    failures += check(api.mapDocumentYToScreen(100.0, "doc-1").alpha == 1.0,
                      "squeeze mapping reports unsqueezed alpha default when no regions set");
    api.setSqueezeRegion("doc-1", 10.0, 20.0, 0.5);
    failures += check(std::abs(api.mapDocumentYToScreen(15.0, "doc-1").alpha - 0.5) < 1e-6,
                      "squeeze mapping reports squeezed alpha in active region");
    api.resetSqueeze("doc-1");
    failures += check(api.mapDocumentYToScreen(15.0, "doc-1").alpha == 1.0,
                      "squeeze mapping resets to alpha 1.0 after resetSqueeze");
    // Graph Edge routing and lifecycle (M4)
    const std::string nodeA =
        api.insertNode(std::make_unique<RectNode>("node-A", Rectangle{50.0, 50.0, 100.0, 80.0}));
    const std::string nodeB =
        api.insertNode(std::make_unique<RectNode>("node-B", Rectangle{300.0, 50.0, 100.0, 80.0}));
    const std::string edgeAB = api.createInkLink(nodeA, nodeB, Color{0, 120, 255, 255});
    failures += check(!edgeAB.empty(), "createInkLink creates valid edge ID");
    failures += check(api.getAllEdges().size() == 1, "getAllEdges returns registered edge");
    failures +=
        check(api.getConnectedEdges(nodeA).size() == 1, "getConnectedEdges finds edge for node A");
    failures +=
        check(api.getConnectedEdges(nodeB).size() == 1, "getConnectedEdges finds edge for node B");

    BezierSpline spline = api.getEdgeGeometry(edgeAB);
    failures += check(spline.controlPoints.size() == 4,
                      "getEdgeGeometry returns 4 cubic Bezier control points");
    // p0 should dock to right perimeter of nodeA (x = 150.0) and p3 to left perimeter of nodeB (x =
    // 300.0)
    failures +=
        check(close(spline.controlPoints.front().x, 150.0), "p0 docks to source node right edge");
    failures +=
        check(close(spline.controlPoints.back().x, 300.0), "p3 docks to target node left edge");

    // Dynamic re-routing: moving nodeB shifts docking points and spline
    api.updateNodePosition(nodeB, 500.0, 50.0);
    BezierSpline movedSpline = api.getEdgeGeometry(edgeAB);
    failures +=
        check(close(movedSpline.controlPoints.back().x, 500.0), "p3 dynamically tracks moved node");

    // Cascading node removal: removing nodeA removes edgeAB
    api.removeNode(nodeA);
    failures += check(api.getAllEdges().empty(), "removeNode cascades deletion to connected edges");
    failures +=
        check(api.getConnectedEdges(nodeB).empty(), "nodeB has no remaining connected edges");
    api.removeNode(nodeB);

    api.openProject("/tmp/nonexistent.ltproj");
    api.saveProject();
    failures += check(api.executeSearch("query").empty(), "search stub empty (M5)");

    if (failures == 0) {
        std::cout << "FluidCoreEngineTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
