#include "geometry/StrokeHitTest.h"

namespace FluidCore {

double pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-12) {
        const double ddx = px - x1;
        const double ddy = py - y1;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    double t = ((px - x1) * dx + (py - y1) * dy) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    const double cx = x1 + t * dx;
    const double cy = y1 + t * dy;
    const double ddx = px - cx;
    const double ddy = py - cy;
    return std::sqrt(ddx * ddx + ddy * ddy);
}

StrokeHitResult testPointAgainstStroke(double px, double py, const Stroke& stroke,
                                       double eraserRadius) {
    if (stroke.points.empty()) {
        return {false, std::numeric_limits<double>::max()};
    }

    const double effectiveThreshold = eraserRadius + (stroke.width / 2.0);

    // Broad-phase: Axis-Aligned Bounding Box (AABB) rejection
    double minX = stroke.points[0].x;
    double maxX = stroke.points[0].x;
    double minY = stroke.points[0].y;
    double maxY = stroke.points[0].y;
    for (const auto& pt : stroke.points) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }

    if (px < minX - effectiveThreshold || px > maxX + effectiveThreshold ||
        py < minY - effectiveThreshold || py > maxY + effectiveThreshold) {
        return {false, std::numeric_limits<double>::max()};
    }

    // Narrow-phase: point-to-segment distance against all vertices
    double minDistance = std::numeric_limits<double>::max();

    if (stroke.points.size() == 1) {
        const double ddx = px - stroke.points[0].x;
        const double ddy = py - stroke.points[0].y;
        minDistance = std::sqrt(ddx * ddx + ddy * ddy);
    } else {
        for (std::size_t i = 0; i + 1 < stroke.points.size(); ++i) {
            const double d = pointToSegmentDistance(px, py, stroke.points[i].x, stroke.points[i].y,
                                                    stroke.points[i + 1].x, stroke.points[i + 1].y);
            if (d < minDistance) {
                minDistance = d;
            }
        }
    }

    const bool hit = (minDistance <= effectiveThreshold);
    return {hit, minDistance};
}

std::vector<StrokeHitMatch>
findStrokesUnderPoint(double px, double py, const std::vector<const Stroke*>& candidateStrokes,
                      double eraserRadius) {
    std::vector<StrokeHitMatch> matches;
    for (const auto* stroke : candidateStrokes) {
        if (!stroke) {
            continue;
        }
        auto res = testPointAgainstStroke(px, py, *stroke, eraserRadius);
        if (res.hit) {
            matches.push_back({stroke->id, res.distance});
        }
    }

    std::sort(matches.begin(), matches.end(), [](const StrokeHitMatch& a, const StrokeHitMatch& b) {
        return a.distance < b.distance;
    });

    return matches;
}

double maxRenderedStrokeWidth(const Stroke& stroke) {
    if (stroke.points.empty()) {
        return 0.0;
    }
    double maxP = 1.0;
    for (double p : stroke.pressures) {
        if (p > maxP) {
            maxP = p;
        }
    }
    // FluidCore Cairo renderer equation: width * (0.25 + 0.75 * p)
    return stroke.width * (0.25 + 0.75 * maxP);
}

Rectangle computeStrokeBounds(const Stroke& stroke) {
    if (stroke.points.empty()) {
        return {0.0, 0.0, 0.0, 0.0};
    }
    double minX = stroke.points[0].x;
    double maxX = stroke.points[0].x;
    double minY = stroke.points[0].y;
    double maxY = stroke.points[0].y;
    for (const auto& pt : stroke.points) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }
    const double pad = (maxRenderedStrokeWidth(stroke) * 0.5) + 1.0; // 1.0pt safety epsilon
    return {minX - pad, minY - pad, (maxX - minX) + 2.0 * pad, (maxY - minY) + 2.0 * pad};
}

bool rectanglesIntersect(const Rectangle& a, const Rectangle& b, double tolerance) {
    return (a.x <= b.x + b.w + tolerance && a.x + a.w + tolerance >= b.x &&
            a.y <= b.y + b.h + tolerance && a.y + a.h + tolerance >= b.y);
}

Rectangle uniteRectangles(const Rectangle& a, const Rectangle& b) {
    if (a.w <= 0.0 || a.h <= 0.0)
        return b;
    if (b.w <= 0.0 || b.h <= 0.0)
        return a;
    const double minX = std::min(a.x, b.x);
    const double minY = std::min(a.y, b.y);
    const double maxX = std::max(a.x + a.w, b.x + b.w);
    const double maxY = std::max(a.y + a.h, b.y + b.h);
    return {minX, minY, maxX - minX, maxY - minY};
}

} // namespace FluidCore
