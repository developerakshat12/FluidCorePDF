#pragma once

#include "FluidCoreAPI.h"
#include "graph/GraphEdge.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace FluidCore {

// Simple directed graph G=(V,E) with bidirectional promotion + reactive cubic Bézier curve router
// (TRD §3.5). Invariant: At most one edge exists between any pair of nodes {A, B}. Adheres to
// ADR-0001 (Zero Cairo/GTK headers in libfluidcore).
class GraphTopology {
  public:
    GraphTopology();
    ~GraphTopology() = default;

    // Registers or promotes a connection between source and target nodes.
    // Invariant: At most one edge exists between {src, dst}.
    // - If no edge exists: creates a new edge with direction = Forward.
    // - If edge exists in same direction: updates color/style, returns existing edge ID.
    // - If edge exists in opposite direction: promotes direction to Bidirectional, returns existing
    // edge ID. Returns edge ID on success, or empty string on failure (e.g. self-loop or empty node
    // IDs).
    std::string addEdge(const GraphEdge& edge);
    std::string addEdge(const std::string& sourceNodeId, const std::string& targetNodeId,
                        const Color& color = {30, 144, 255, 255}, double strokeWidth = 2.0,
                        ArrowStyle arrowStyle = ArrowStyle::SharpTriangle,
                        const std::string& label = "");

    bool removeEdge(const std::string& edgeId);

    // Cascading removal: removes all edges where nodeId is source or target.
    // Returns list of removed edge IDs (for undo/redo tracking).
    std::vector<std::string> removeEdgesForNode(const std::string& nodeId);

    std::optional<GraphEdge> findEdge(const std::string& edgeId) const;
    std::optional<GraphEdge> findEdgeBetween(const std::string& nodeA,
                                             const std::string& nodeB) const;

    std::vector<std::string> allEdgeIds() const;
    std::vector<std::string> connectedEdgeIds(const std::string& nodeId) const;
    std::vector<std::string> outEdgeIds(const std::string& nodeId) const;
    std::vector<std::string> inEdgeIds(const std::string& nodeId) const;

    std::size_t edgeCount() const { return m_edges.size(); }
    void clear();

    // Cubic Bézier routing with perimeter docking, tangent calculations,
    // and degenerate distance / overlap guards.
    BezierSpline computeEdgeSpline(const std::string& edgeId, const Rectangle& srcBounds,
                                   const Rectangle& dstBounds) const;

    // Direct routing geometry computation between two rectangles.
    static BezierSpline computeSplineBetweenBounds(const Rectangle& srcBounds,
                                                   const Rectangle& dstBounds);

    // Calculates intersection point of line segment (from center to otherCenter)
    // with the rectangle's boundary box.
    static Point intersectRectPerimeter(const Rectangle& rect, const Point& otherCenter);

    // Hit-tests a Bezier spline against a world point within a given distance tolerance.
    static bool hitTestSpline(const BezierSpline& spline, const Point& worldPt,
                              double tolerance = 8.0);

  private:
    std::unordered_map<std::string, GraphEdge> m_edges;
    std::unordered_map<std::string, std::vector<std::string>> m_outEdges;
    std::unordered_map<std::string, std::vector<std::string>> m_inEdges;
    std::size_t m_edgeCounter = 0;
};

} // namespace FluidCore
