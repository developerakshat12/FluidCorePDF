#pragma once

#include "FluidCoreAPI.h"
#include "services/StrokeStabilizer.h"
#include "storage/AnnotationStore.h"
#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace FluidCoreApp {

struct StrokeClipBounds {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool valid = false;
};

// Computes the safety padding: (strokeWidth * 0.5) + antialiasMargin.
// Default antialiasMargin = 4.0 ensures Bézier smoothing, round caps, and edge AA are fully enclosed.
inline double calculateStrokePadding(double strokeWidth, double antialiasMargin = 4.0) {
    return (std::max(0.5, strokeWidth) * 0.5) + antialiasMargin;
}

// Expands a base bounding box with padding derived from strokeWidth.
inline StrokeClipBounds expandBounds(double minX, double minY, double maxX, double maxY,
                                     double strokeWidth, double antialiasMargin = 4.0) {
    const double pad = calculateStrokePadding(strokeWidth, antialiasMargin);
    return {
        minX - pad,
        minY - pad,
        (maxX - minX) + 2.0 * pad,
        (maxY - minY) + 2.0 * pad,
        true
    };
}

// Computes bounding box for a completed Stroke in its local coordinate system.
inline StrokeClipBounds computeStrokeClipBounds(const FluidCore::Stroke& stroke,
                                                double antialiasMargin = 4.0) {
    if (stroke.points.empty()) {
        return {0.0, 0.0, 0.0, 0.0, false};
    }
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& pt : stroke.points) {
        if (pt.x < minX) minX = pt.x;
        if (pt.x > maxX) maxX = pt.x;
        if (pt.y < minY) minY = pt.y;
        if (pt.y > maxY) maxY = pt.y;
    }
    return expandBounds(minX, minY, maxX, maxY, stroke.width, antialiasMargin);
}

// Computes bounding box for an active wet stroke (samples + optional wet tip point).
inline StrokeClipBounds computeWetStrokeClipBounds(
    const std::vector<StrokeStabilizer::StabilizedSample>& samples,
    bool hasWetTip,
    const StrokeStabilizer::Point2D& wetTip,
    double strokeWidth,
    double antialiasMargin = 4.0) {

    if (samples.empty() && !hasWetTip) {
        return {0.0, 0.0, 0.0, 0.0, false};
    }
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    bool hasAnyPoint = false;

    for (const auto& s : samples) {
        hasAnyPoint = true;
        if (s.point.x < minX) minX = s.point.x;
        if (s.point.x > maxX) maxX = s.point.x;
        if (s.point.y < minY) minY = s.point.y;
        if (s.point.y > maxY) maxY = s.point.y;
    }
    if (hasWetTip) {
        hasAnyPoint = true;
        if (wetTip.x < minX) minX = wetTip.x;
        if (wetTip.x > maxX) maxX = wetTip.x;
        if (wetTip.y < minY) minY = wetTip.y;
        if (wetTip.y > maxY) maxY = wetTip.y;
    }

    if (!hasAnyPoint) {
        return {0.0, 0.0, 0.0, 0.0, false};
    }
    return expandBounds(minX, minY, maxX, maxY, strokeWidth, antialiasMargin);
}

} // namespace FluidCoreApp
