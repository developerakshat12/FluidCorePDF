#pragma once

#include "FluidCoreAPI.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace FluidCore {

// Pure C++20 spatial proximity and magnetic snapping physics solver (specs/new-features-backlog.md
// §2.1, TRD §3.4). Evaluates 16pt magnetic snapping alignments and >50% area overlap stack-drop
// detection. ADR-0001 compliant: zero Cairo/GTK dependencies.
class PhysicsSolver {
  public:
    static constexpr double kDefaultSnapThreshold = 16.0;
    static constexpr double kDefaultStackOverlapThreshold = 0.50;

    // Symmetric overlap area ratio: Area(Intersection) / min(Area(A), Area(B))
    static double calculateOverlapRatio(const Rectangle& r1, const Rectangle& r2);

    // Bounding box intersection rectangle
    static Rectangle calculateIntersection(const Rectangle& r1, const Rectangle& r2);

    // Solves proximity against candidates.
    // If overlap > overlapThreshold, returns StackMerge.
    // Otherwise evaluates horizontal and vertical magnetic edge alignments within snapThreshold.
    static SnapResult solveSnap(const Rectangle& dragBounds,
                                const std::vector<CandidateTarget>& candidates,
                                double snapThreshold = kDefaultSnapThreshold,
                                double overlapThreshold = kDefaultStackOverlapThreshold,
                                const std::string& ignoreId = "");
};

} // namespace FluidCore
