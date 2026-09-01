#include "GraphTopology.h"

#include <algorithm>
#include <cmath>

namespace FluidCore {

GraphTopology::GraphTopology() = default;

std::optional<GraphEdge> GraphTopology::findEdgeBetween(const std::string& nodeA,
                                                        const std::string& nodeB) const {
    if (nodeA.empty() || nodeB.empty() || nodeA == nodeB) {
        return std::nullopt;
    }

    // Check outgoing edges from nodeA
    auto outIt = m_outEdges.find(nodeA);
    if (outIt != m_outEdges.end()) {
        for (const auto& eid : outIt->second) {
            auto it = m_edges.find(eid);
            if (it != m_edges.end() && it->second.targetNodeId == nodeB) {
                return it->second;
            }
        }
    }

    // Check outgoing edges from nodeB (reverse direction)
    auto inIt = m_outEdges.find(nodeB);
    if (inIt != m_outEdges.end()) {
        for (const auto& eid : inIt->second) {
            auto it = m_edges.find(eid);
            if (it != m_edges.end() && it->second.targetNodeId == nodeA) {
                return it->second;
            }
        }
    }

    return std::nullopt;
}

std::string GraphTopology::addEdge(const GraphEdge& edge) {
    if (edge.sourceNodeId.empty() || edge.targetNodeId.empty()) {
        return {};
    }
    // Reject self-loops per specification
    if (edge.sourceNodeId == edge.targetNodeId) {
        return {};
    }

    // Single-edge invariant check: check if an edge already connects these two nodes
    auto existingOpt = findEdgeBetween(edge.sourceNodeId, edge.targetNodeId);
    if (existingOpt.has_value()) {
        const std::string existingId = existingOpt->id;
        auto it = m_edges.find(existingId);
        if (it != m_edges.end()) {
            if (it->second.sourceNodeId == edge.sourceNodeId &&
                it->second.targetNodeId == edge.targetNodeId) {
                // Same direction: refresh color/style/label without duplicating
                it->second.color = edge.color;
                it->second.strokeWidth = edge.strokeWidth;
                it->second.arrowStyle = edge.arrowStyle;
                if (!edge.label.empty()) {
                    it->second.label = edge.label;
                }
                return existingId;
            } else if (it->second.sourceNodeId == edge.targetNodeId &&
                       it->second.targetNodeId == edge.sourceNodeId) {
                // Opposite direction: promote to bidirectional
                it->second.direction = EdgeDirection::Bidirectional;
                it->second.color = edge.color;
                it->second.strokeWidth = edge.strokeWidth;
                if (!edge.label.empty()) {
                    it->second.label = edge.label;
                }
                return existingId;
            }
        }
    }

    std::string edgeId = edge.id;
    if (edgeId.empty()) {
        edgeId = "edge-" + std::to_string(++m_edgeCounter);
    }

    GraphEdge storedEdge = edge;
    storedEdge.id = edgeId;
    if (storedEdge.direction != EdgeDirection::Bidirectional) {
        storedEdge.direction = EdgeDirection::Forward;
    }

    m_edges[edgeId] = storedEdge;
    m_outEdges[storedEdge.sourceNodeId].push_back(edgeId);
    m_inEdges[storedEdge.targetNodeId].push_back(edgeId);

    return edgeId;
}

std::string GraphTopology::addEdge(const std::string& sourceNodeId, const std::string& targetNodeId,
                                   const Color& color, double strokeWidth, ArrowStyle arrowStyle,
                                   const std::string& label) {
    GraphEdge edge;
    edge.sourceNodeId = sourceNodeId;
    edge.targetNodeId = targetNodeId;
    edge.color = color;
    edge.strokeWidth = strokeWidth;
    edge.arrowStyle = arrowStyle;
    edge.label = label;
    return addEdge(edge);
}

bool GraphTopology::removeEdge(const std::string& edgeId) {
    auto it = m_edges.find(edgeId);
    if (it == m_edges.end()) {
        return false;
    }

    const std::string src = it->second.sourceNodeId;
    const std::string dst = it->second.targetNodeId;

    m_edges.erase(it);

    auto removeId = [&](std::vector<std::string>& vec) {
        vec.erase(std::remove(vec.begin(), vec.end(), edgeId), vec.end());
    };

    auto outIt = m_outEdges.find(src);
    if (outIt != m_outEdges.end()) {
        removeId(outIt->second);
        if (outIt->second.empty()) {
            m_outEdges.erase(outIt);
        }
    }

    auto inIt = m_inEdges.find(dst);
    if (inIt != m_inEdges.end()) {
        removeId(inIt->second);
        if (inIt->second.empty()) {
            m_inEdges.erase(inIt);
        }
    }

    return true;
}

std::vector<std::string> GraphTopology::removeEdgesForNode(const std::string& nodeId) {
    std::vector<std::string> connected = connectedEdgeIds(nodeId);
    for (const auto& edgeId : connected) {
        removeEdge(edgeId);
    }
    return connected;
}

std::optional<GraphEdge> GraphTopology::findEdge(const std::string& edgeId) const {
    auto it = m_edges.find(edgeId);
    if (it != m_edges.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<std::string> GraphTopology::allEdgeIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_edges.size());
    for (const auto& [id, _] : m_edges) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> GraphTopology::connectedEdgeIds(const std::string& nodeId) const {
    std::vector<std::string> result;
    auto outIt = m_outEdges.find(nodeId);
    if (outIt != m_outEdges.end()) {
        result.insert(result.end(), outIt->second.begin(), outIt->second.end());
    }
    auto inIt = m_inEdges.find(nodeId);
    if (inIt != m_inEdges.end()) {
        for (const auto& id : inIt->second) {
            if (std::find(result.begin(), result.end(), id) == result.end()) {
                result.push_back(id);
            }
        }
    }
    return result;
}

std::vector<std::string> GraphTopology::outEdgeIds(const std::string& nodeId) const {
    auto it = m_outEdges.find(nodeId);
    if (it != m_outEdges.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> GraphTopology::inEdgeIds(const std::string& nodeId) const {
    auto it = m_inEdges.find(nodeId);
    if (it != m_inEdges.end()) {
        return it->second;
    }
    return {};
}

void GraphTopology::clear() {
    m_edges.clear();
    m_outEdges.clear();
    m_inEdges.clear();
}

Point GraphTopology::intersectRectPerimeter(const Rectangle& rect, const Point& otherCenter) {
    const Point center{rect.x + rect.w / 2.0, rect.y + rect.h / 2.0};
    const double dx = otherCenter.x - center.x;
    const double dy = otherCenter.y - center.y;

    if (std::abs(dx) < 1e-6 && std::abs(dy) < 1e-6) {
        return center;
    }

    const double halfW = std::max(rect.w / 2.0, 1.0);
    const double halfH = std::max(rect.h / 2.0, 1.0);

    // Ray from center: p(t) = center + t * (dx, dy)
    // Find smallest positive t that hits rectangle border
    double tx = std::abs(dx) > 1e-6 ? (halfW / std::abs(dx)) : 1e9;
    double ty = std::abs(dy) > 1e-6 ? (halfH / std::abs(dy)) : 1e9;
    double t = std::min(tx, ty);

    return {center.x + dx * t, center.y + dy * t};
}

BezierSpline GraphTopology::computeSplineBetweenBounds(const Rectangle& srcBounds,
                                                       const Rectangle& dstBounds) {
    const Point cSrc{srcBounds.x + srcBounds.w / 2.0, srcBounds.y + srcBounds.h / 2.0};
    const Point cDst{dstBounds.x + dstBounds.w / 2.0, dstBounds.y + dstBounds.h / 2.0};

    Point p0 = intersectRectPerimeter(srcBounds, cDst);
    Point p3 = intersectRectPerimeter(dstBounds, cSrc);

    const double dx = p3.x - p0.x;
    const double dy = p3.y - p0.y;
    const double dist = std::sqrt(dx * dx + dy * dy);

    // Degenerate geometry guard: if cards overlap or endpoints are too close (< 8pt)
    if (dist < 8.0) {
        // Fallback straight line
        Point p1{p0.x + dx / 3.0, p0.y + dy / 3.0};
        Point p2{p0.x + 2.0 * dx / 3.0, p0.y + 2.0 * dy / 3.0};
        return BezierSpline{{p0, p1, p2, p3}};
    }

    const double unitX = dx / dist;
    const double unitY = dy / dist;

    // Tangent handle tension
    double handleDist = std::min(dist * 0.45, 160.0);
    handleDist = std::max(handleDist, 20.0);

    // Single optimal cubic Bézier curve
    Point p1{p0.x + unitX * handleDist, p0.y + unitY * handleDist};
    Point p2{p3.x - unitX * handleDist, p3.y - unitY * handleDist};

    return BezierSpline{{p0, p1, p2, p3}};
}

BezierSpline GraphTopology::computeEdgeSpline(const std::string& edgeId, const Rectangle& srcBounds,
                                              const Rectangle& dstBounds) const {
    auto edgeOpt = findEdge(edgeId);
    if (!edgeOpt.has_value()) {
        return {};
    }

    return computeSplineBetweenBounds(srcBounds, dstBounds);
}

bool GraphTopology::hitTestSpline(const BezierSpline& spline, const Point& worldPt,
                                  double tolerance) {
    if (spline.controlPoints.size() < 4) {
        return false;
    }

    const auto& p0 = spline.controlPoints[0];
    const auto& p1 = spline.controlPoints[1];
    const auto& p2 = spline.controlPoints[2];
    const auto& p3 = spline.controlPoints[3];

    const double tolSq = tolerance * tolerance;
    constexpr int kSamples = 24;

    Point prevPt = p0;
    for (int i = 1; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples);
        const double oneMinusT = 1.0 - t;
        const double c0 = oneMinusT * oneMinusT * oneMinusT;
        const double c1 = 3.0 * oneMinusT * oneMinusT * t;
        const double c2 = 3.0 * oneMinusT * t * t;
        const double c3 = t * t * t;

        Point currPt{c0 * p0.x + c1 * p1.x + c2 * p2.x + c3 * p3.x,
                     c0 * p0.y + c1 * p1.y + c2 * p2.y + c3 * p3.y};

        // Distance from worldPt to segment (prevPt, currPt)
        const double segDx = currPt.x - prevPt.x;
        const double segDy = currPt.y - prevPt.y;
        const double segLenSq = segDx * segDx + segDy * segDy;

        double distSq = 0.0;
        if (segLenSq < 1e-8) {
            const double dpx = worldPt.x - prevPt.x;
            const double dpy = worldPt.y - prevPt.y;
            distSq = dpx * dpx + dpy * dpy;
        } else {
            const double u = std::clamp(
                ((worldPt.x - prevPt.x) * segDx + (worldPt.y - prevPt.y) * segDy) / segLenSq, 0.0,
                1.0);
            const double projX = prevPt.x + u * segDx;
            const double projY = prevPt.y + u * segDy;
            const double dpx = worldPt.x - projX;
            const double dpy = worldPt.y - projY;
            distSq = dpx * dpx + dpy * dpy;
        }

        if (distSq <= tolSq) {
            return true;
        }

        prevPt = currPt;
    }

    return false;
}

} // namespace FluidCore
