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

} // namespace FluidCore
