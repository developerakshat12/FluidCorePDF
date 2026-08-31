#include "graph/GraphTopology.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace FluidCore;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

bool close(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

} // namespace

int main() {
    int failures = 0;

    GraphTopology graph;

    // 1. Add, query, and remove edges
    std::string e1 = graph.addEdge("node-1", "node-2", Color{255, 0, 0, 255}, 2.0);
    failures += check(!e1.empty(), "addEdge creates valid edge ID");
    failures += check(graph.edgeCount() == 1, "edgeCount reflects added edge");
    failures += check(graph.allEdgeIds().size() == 1, "allEdgeIds returns 1 edge");

    auto edgeOpt = graph.findEdge(e1);
    failures += check(edgeOpt.has_value(), "findEdge finds edge by ID");
    failures += check(edgeOpt->sourceNodeId == "node-1", "source node matches");
    failures += check(edgeOpt->targetNodeId == "node-2", "target node matches");
    failures += check(edgeOpt->direction == EdgeDirection::Forward, "initial direction is Forward");
    failures += check(edgeOpt->color.r == 255, "color matches");

    failures +=
        check(graph.connectedEdgeIds("node-1").size() == 1, "connected edges for node-1 is 1");
    failures +=
        check(graph.connectedEdgeIds("node-2").size() == 1, "connected edges for node-2 is 1");
    failures += check(graph.outEdgeIds("node-1").size() == 1, "out edges for node-1 is 1");
    failures += check(graph.inEdgeIds("node-2").size() == 1, "in edges for node-2 is 1");

    // 2. Self-loop rejection
    std::string selfLoop = graph.addEdge("node-1", "node-1");
    failures += check(selfLoop.empty(), "self loop is rejected");
    failures += check(graph.edgeCount() == 1, "edgeCount remains 1 after self-loop attempt");

    // 3. Single-edge invariant & Redundant stroke deduplication
    std::string e1Redundant = graph.addEdge("node-1", "node-2", Color{0, 255, 0, 255}, 2.5);
    failures += check(e1Redundant == e1, "redundant edge returns same edge ID");
    failures += check(graph.edgeCount() == 1, "edgeCount remains 1 on redundant stroke");
    auto e1Refreshed = graph.findEdge(e1);
    failures += check(e1Refreshed->color.g == 255, "color updated on redundant stroke");
    failures +=
        check(e1Refreshed->direction == EdgeDirection::Forward, "direction remains Forward");

    // 4. Forward-First Promotion (A -> B then B -> A)
    std::string e1Promoted = graph.addEdge("node-2", "node-1", Color{0, 0, 255, 255}, 3.0);
    failures += check(e1Promoted == e1, "reverse stroke promotes same edge ID");
    failures += check(graph.edgeCount() == 1, "edgeCount remains 1 after promotion");
    auto e1Bi = graph.findEdge(e1);
    failures += check(e1Bi->direction == EdgeDirection::Bidirectional, "promoted to Bidirectional");

    // 5. Reverse-First Promotion (B -> A then A -> B on a new node pair)
    std::string e2 = graph.addEdge("node-3", "node-4", Color{100, 100, 100, 255});
    failures +=
        check(graph.findEdge(e2)->direction == EdgeDirection::Forward, "node-3->node-4 is Forward");
    std::string e2Promoted = graph.addEdge("node-4", "node-3", Color{200, 200, 200, 255});
    failures += check(e2Promoted == e2, "reverse-first stroke promotes same edge ID");
    failures += check(graph.findEdge(e2)->direction == EdgeDirection::Bidirectional,
                      "node-3<->node-4 promoted to Bidirectional");

    // 6. Perimeter docking and single spline geometry
    Rectangle rect1{100.0, 100.0, 100.0, 60.0}; // center (150, 130)
    Rectangle rect2{400.0, 100.0, 100.0, 60.0}; // center (450, 130)

    BezierSpline s1 = graph.computeEdgeSpline(e1, rect1, rect2);
    failures += check(s1.controlPoints.size() == 4, "spline has 4 control points");

    // Endpoints dock on boundary (x=200 on rect1 right edge, x=400 on rect2 left edge)
    failures += check(close(s1.controlPoints[0].x, 200.0), "s1 p0 docks at right boundary");
    failures += check(close(s1.controlPoints[3].x, 400.0), "s1 p3 docks at left boundary");

    // 7. Spline hit-testing (world space polyline distance)
    // Point on line between (200, 130) and (400, 130) is approximately (300, 130)
    failures += check(GraphTopology::hitTestSpline(s1, Point{300.0, 130.0}, 8.0),
                      "hitTestSpline succeeds near curve");
    failures += check(GraphTopology::hitTestSpline(s1, Point{300.0, 135.0}, 8.0),
                      "hitTestSpline succeeds within 8pt");
    failures += check(!GraphTopology::hitTestSpline(s1, Point{300.0, 160.0}, 8.0),
                      "hitTestSpline fails when far from curve");

    // 8. Degenerate distance & overlapping nodes guard
    Rectangle rectOverlap1{100.0, 100.0, 100.0, 60.0};
    Rectangle rectOverlap2{104.0, 102.0, 100.0, 60.0};
    BezierSpline sOverlap = graph.computeEdgeSpline(e1, rectOverlap1, rectOverlap2);
    failures +=
        check(sOverlap.controlPoints.size() == 4, "overlap produces 4 valid control points");
    for (const auto& pt : sOverlap.controlPoints) {
        failures += check(!std::isnan(pt.x) && !std::isnan(pt.y), "overlap points are not NaN");
    }

    // 9. Node cascading edge removal
    std::vector<std::string> removed = graph.removeEdgesForNode("node-2");
    failures += check(removed.size() == 1, "removeEdgesForNode removes e1");
    failures += check(graph.edgeCount() == 1, "graph has 1 edge left (e2)");
    failures +=
        check(graph.connectedEdgeIds("node-1").empty(), "node-1 has no connected edges left");

    if (failures == 0) {
        std::cout << "GraphTopologyTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
