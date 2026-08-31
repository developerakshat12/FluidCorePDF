#include "workspace/PhysicsSolver.h"

#include <algorithm>
#include <cmath>

namespace FluidCore {

Rectangle PhysicsSolver::calculateIntersection(const Rectangle& r1, const Rectangle& r2) {
    const double x1 = std::max(r1.x, r2.x);
    const double y1 = std::max(r1.y, r2.y);
    const double x2 = std::min(r1.x + r1.w, r2.x + r2.w);
    const double y2 = std::min(r1.y + r1.h, r2.y + r2.h);

    if (x2 > x1 && y2 > y1) {
        return Rectangle{x1, y1, x2 - x1, y2 - y1};
    }
    return Rectangle{0.0, 0.0, 0.0, 0.0};
}

double PhysicsSolver::calculateOverlapRatio(const Rectangle& r1, const Rectangle& r2) {
    const double area1 = r1.w * r1.h;
    const double area2 = r2.w * r2.h;
    if (area1 <= 0.0 || area2 <= 0.0) {
        return 0.0;
    }

    const Rectangle inter = calculateIntersection(r1, r2);
    const double interArea = inter.w * inter.h;
    if (interArea <= 0.0) {
        return 0.0;
    }

    const double minArea = std::min(area1, area2);
    return interArea / minArea;
}

SnapResult PhysicsSolver::solveSnap(const Rectangle& dragBounds,
                                    const std::vector<CandidateTarget>& candidates,
                                    double snapThreshold, double overlapThreshold,
                                    const std::string& ignoreId) {
    SnapResult result;
    result.snappedBounds = dragBounds;

    if (candidates.empty()) {
        return result;
    }

    // 1. Check for stack merge overlap (> overlapThreshold) — Precedence rule:
    // If overlap > threshold, stack merge wins and magnetic edge snapping is bypassed.
    double maxOverlap = 0.0;
    const CandidateTarget* bestMergeTarget = nullptr;

    for (const auto& cand : candidates) {
        if (!ignoreId.empty() && cand.id == ignoreId) {
            continue;
        }
        const double overlap = calculateOverlapRatio(dragBounds, cand.bounds);
        if (overlap > overlapThreshold && overlap > maxOverlap) {
            maxOverlap = overlap;
            bestMergeTarget = &cand;
        }
    }

    if (bestMergeTarget != nullptr) {
        result.type = SnapType::StackMerge;
        result.targetNodeId = bestMergeTarget->id;
        result.overlapRatio = maxOverlap;
        result.snappedBounds = bestMergeTarget->bounds;
        return result;
    }

    // 2. Evaluate magnetic edge snapping (Delta <= snapThreshold)
    double bestDeltaX = snapThreshold + 1.0;
    double snappedX = dragBounds.x;
    std::vector<SnapGuideLine> xGuidelines;

    double bestDeltaY = snapThreshold + 1.0;
    double snappedY = dragBounds.y;
    std::vector<SnapGuideLine> yGuidelines;

    for (const auto& cand : candidates) {
        if (!ignoreId.empty() && cand.id == ignoreId) {
            continue;
        }

        const Rectangle& t = cand.bounds;

        // Skip candidates that are too far away to influence snapping (optimization)
        const double maxSearchDist = std::max(dragBounds.w, dragBounds.h) + snapThreshold * 4.0;
        if (std::abs(dragBounds.x - t.x) > maxSearchDist + t.w ||
            std::abs(dragBounds.y - t.y) > maxSearchDist + t.h) {
            continue;
        }

        // --- Horizontal Snapping ---
        // a) Left-to-Left alignment
        double d = std::abs(dragBounds.x - t.x);
        if (d <= snapThreshold && d < bestDeltaX) {
            bestDeltaX = d;
            snappedX = t.x;
            xGuidelines.clear();
            const double minY = std::min(dragBounds.y, t.y);
            const double maxY = std::max(dragBounds.y + dragBounds.h, t.y + t.h);
            xGuidelines.push_back(
                SnapGuideLine{Point{t.x, minY - 8.0}, Point{t.x, maxY + 8.0}, true});
        }

        // b) Right-to-Right alignment
        d = std::abs((dragBounds.x + dragBounds.w) - (t.x + t.w));
        if (d <= snapThreshold && d < bestDeltaX) {
            bestDeltaX = d;
            snappedX = t.x + t.w - dragBounds.w;
            xGuidelines.clear();
            const double rightX = t.x + t.w;
            const double minY = std::min(dragBounds.y, t.y);
            const double maxY = std::max(dragBounds.y + dragBounds.h, t.y + t.h);
            xGuidelines.push_back(
                SnapGuideLine{Point{rightX, minY - 8.0}, Point{rightX, maxY + 8.0}, true});
        }

        // c) Center-to-Center X alignment
        d = std::abs((dragBounds.x + dragBounds.w * 0.5) - (t.x + t.w * 0.5));
        if (d <= snapThreshold && d < bestDeltaX) {
            bestDeltaX = d;
            snappedX = t.x + (t.w - dragBounds.w) * 0.5;
            xGuidelines.clear();
            const double centerX = t.x + t.w * 0.5;
            const double minY = std::min(dragBounds.y, t.y);
            const double maxY = std::max(dragBounds.y + dragBounds.h, t.y + t.h);
            xGuidelines.push_back(
                SnapGuideLine{Point{centerX, minY - 8.0}, Point{centerX, maxY + 8.0}, true});
        }

        // d) Dock Right (Drag Left to Target Right)
        d = std::abs(dragBounds.x - (t.x + t.w));
        if (d <= snapThreshold && d < bestDeltaX) {
            bestDeltaX = d;
            snappedX = t.x + t.w;
            xGuidelines.clear();
            const double dockX = t.x + t.w;
            const double minY = std::min(dragBounds.y, t.y);
            const double maxY = std::max(dragBounds.y + dragBounds.h, t.y + t.h);
            xGuidelines.push_back(
                SnapGuideLine{Point{dockX, minY - 8.0}, Point{dockX, maxY + 8.0}, true});
        }

        // e) Dock Left (Drag Right to Target Left)
        d = std::abs((dragBounds.x + dragBounds.w) - t.x);
        if (d <= snapThreshold && d < bestDeltaX) {
            bestDeltaX = d;
            snappedX = t.x - dragBounds.w;
            xGuidelines.clear();
            const double dockX = t.x;
            const double minY = std::min(dragBounds.y, t.y);
            const double maxY = std::max(dragBounds.y + dragBounds.h, t.y + t.h);
            xGuidelines.push_back(
                SnapGuideLine{Point{dockX, minY - 8.0}, Point{dockX, maxY + 8.0}, true});
        }

        // --- Vertical Snapping ---
        // a) Top-to-Top alignment
        d = std::abs(dragBounds.y - t.y);
        if (d <= snapThreshold && d < bestDeltaY) {
            bestDeltaY = d;
            snappedY = t.y;
            yGuidelines.clear();
            const double minX = std::min(dragBounds.x, t.x);
            const double maxX = std::max(dragBounds.x + dragBounds.w, t.x + t.w);
            yGuidelines.push_back(
                SnapGuideLine{Point{minX - 8.0, t.y}, Point{maxX + 8.0, t.y}, false});
        }

        // b) Bottom-to-Bottom alignment
        d = std::abs((dragBounds.y + dragBounds.h) - (t.y + t.h));
        if (d <= snapThreshold && d < bestDeltaY) {
            bestDeltaY = d;
            snappedY = t.y + t.h - dragBounds.h;
            yGuidelines.clear();
            const double bottomY = t.y + t.h;
            const double minX = std::min(dragBounds.x, t.x);
            const double maxX = std::max(dragBounds.x + dragBounds.w, t.x + t.w);
            yGuidelines.push_back(
                SnapGuideLine{Point{minX - 8.0, bottomY}, Point{maxX + 8.0, bottomY}, false});
        }

        // c) Center-to-Center Y alignment
        d = std::abs((dragBounds.y + dragBounds.h * 0.5) - (t.y + t.h * 0.5));
        if (d <= snapThreshold && d < bestDeltaY) {
            bestDeltaY = d;
            snappedY = t.y + (t.h - dragBounds.h) * 0.5;
            yGuidelines.clear();
            const double centerY = t.y + t.h * 0.5;
            const double minX = std::min(dragBounds.x, t.x);
            const double maxX = std::max(dragBounds.x + dragBounds.w, t.x + t.w);
            yGuidelines.push_back(
                SnapGuideLine{Point{minX - 8.0, centerY}, Point{maxX + 8.0, centerY}, false});
        }

        // d) Dock Below (Drag Top to Target Bottom)
        d = std::abs(dragBounds.y - (t.y + t.h));
        if (d <= snapThreshold && d < bestDeltaY) {
            bestDeltaY = d;
            snappedY = t.y + t.h;
            yGuidelines.clear();
            const double dockY = t.y + t.h;
            const double minX = std::min(dragBounds.x, t.x);
            const double maxX = std::max(dragBounds.x + dragBounds.w, t.x + t.w);
            yGuidelines.push_back(
                SnapGuideLine{Point{minX - 8.0, dockY}, Point{maxX + 8.0, dockY}, false});
        }

        // e) Dock Above (Drag Bottom to Target Top)
        d = std::abs((dragBounds.y + dragBounds.h) - t.y);
        if (d <= snapThreshold && d < bestDeltaY) {
            bestDeltaY = d;
            snappedY = t.y - dragBounds.h;
            yGuidelines.clear();
            const double dockY = t.y;
            const double minX = std::min(dragBounds.x, t.x);
            const double maxX = std::max(dragBounds.x + dragBounds.w, t.x + t.w);
            yGuidelines.push_back(
                SnapGuideLine{Point{minX - 8.0, dockY}, Point{maxX + 8.0, dockY}, false});
        }
    }

    const bool hasSnapX = (bestDeltaX <= snapThreshold);
    const bool hasSnapY = (bestDeltaY <= snapThreshold);

    if (hasSnapX || hasSnapY) {
        result.type = SnapType::MagneticSnap;
        result.snappedBounds = Rectangle{snappedX, snappedY, dragBounds.w, dragBounds.h};
        for (const auto& g : xGuidelines) {
            result.guideLines.push_back(g);
        }
        for (const auto& g : yGuidelines) {
            result.guideLines.push_back(g);
        }
    }

    return result;
}

} // namespace FluidCore
