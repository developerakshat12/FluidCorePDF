#pragma once

#include "FluidCoreAPI.h"
#include "storage/AnnotationStore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace FluidCore {

// Point-to-segment distance clamped to the segment endpoints [(x1, y1), (x2, y2)].
// Handles degenerate zero-length segments safely (single point / tap).
double pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2);

struct StrokeHitResult {
    bool hit = false;
    double distance = std::numeric_limits<double>::max();
};

// Evaluates whether point (px, py) touches or falls within eraser radius of a stroke.
// Two-phase check:
// 1. Broad-phase AABB reject against stroke's bounding box expanded by (eraserRadius + stroke.width
// / 2.0).
// 2. Narrow-phase clamped distance to every segment.
// Returns hit = true if distance <= eraserRadius + stroke.width / 2.0.
StrokeHitResult testPointAgainstStroke(double px, double py, const Stroke& stroke,
                                       double eraserRadius);

struct StrokeHitMatch {
    std::string strokeId;
    double distance = 0.0;
};

// Narrow-phase filter across candidate strokes. Returns all strokes whose drawn ink
// falls within eraserRadius, sorted nearest-first.
std::vector<StrokeHitMatch>
findStrokesUnderPoint(double px, double py, const std::vector<const Stroke*>& candidateStrokes,
                      double eraserRadius);

// Returns the maximum physical stroke width as rendered by Cairo,
// accounting for dynamic pressure modulation: stroke.width * (0.25 + 0.75 * maxP).
double maxRenderedStrokeWidth(const Stroke& stroke);

// Computes the conservative Axis-Aligned Bounding Box (AABB) in page points
// with pad = (maxRenderedStrokeWidth(stroke) / 2.0) + 1.0pt epsilon.
Rectangle computeStrokeBounds(const Stroke& stroke);

// Evaluates inclusive rectangle intersection with a tolerance margin (default 0.5pt).
bool rectanglesIntersect(const Rectangle& a, const Rectangle& b, double tolerance = 0.5);

// Calculates the minimal bounding box enclosing both rectangles.
Rectangle uniteRectangles(const Rectangle& a, const Rectangle& b);

} // namespace FluidCore
