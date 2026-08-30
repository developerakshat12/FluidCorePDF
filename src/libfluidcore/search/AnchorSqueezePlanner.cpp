#include "search/AnchorSqueezePlanner.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace FluidCore {

std::vector<SqueezeRegion>
AnchorSqueezePlanner::computeAnchorSqueezeRegions(double totalDocHeight,
                                                  const std::vector<AnchorSpan>& anchors,
                                                  const AnchorSqueezeConfig& config) {
    std::vector<SqueezeRegion> gapRegions;
    if (anchors.empty() || totalDocHeight <= 0.0) {
        return gapRegions;
    }

    // Step 1: Pre-sort anchors by docYStart to guarantee monotonic interval processing
    std::vector<AnchorSpan> sortedAnchors = anchors;
    std::sort(sortedAnchors.begin(), sortedAnchors.end(),
              [](const AnchorSpan& a, const AnchorSpan& b) {
                  if (std::abs(a.docYStart - b.docYStart) > 1e-6) {
                      return a.docYStart < b.docYStart;
                  }
                  return a.priority > b.priority;
              });

    // Step 2: Expand each anchor by context padding and clamp to document bounds [0, totalDocHeight]
    struct Interval {
        double y0;
        double y1;
    };
    std::vector<Interval> expanded;
    expanded.reserve(sortedAnchors.size());

    for (const auto& a : sortedAnchors) {
        const double aMin = std::min(a.docYStart, a.docYEnd);
        const double aMax = std::max(a.docYStart, a.docYEnd);
        const double y0 = std::clamp(aMin - config.contextPadding, 0.0, totalDocHeight);
        const double y1 = std::clamp(aMax + config.contextPadding, 0.0, totalDocHeight);
        if (y1 > y0) {
            expanded.push_back({y0, y1});
        }
    }

    if (expanded.empty()) {
        return gapRegions;
    }

    // Step 3: Perform continuous interval union on uncollapsed anchor windows
    std::vector<Interval> merged;
    merged.reserve(expanded.size());

    for (const auto& iv : expanded) {
        if (merged.empty() || iv.y0 > merged.back().y1) {
            merged.push_back(iv);
        } else {
            merged.back().y1 = std::max(merged.back().y1, iv.y1);
        }
    }

    // Step 4: Invert merged uncollapsed windows into gap squeeze regions
    double curDocY = 0.0;
    std::size_t gapIdx = 1;

    for (const auto& m : merged) {
        if (m.y0 - curDocY >= config.minGapHeight) {
            gapRegions.push_back(SqueezeRegion{"anchor-gap-" + std::to_string(gapIdx++), curDocY,
                                               m.y0, config.gapAlpha});
        }
        curDocY = m.y1;
    }

    if (totalDocHeight - curDocY >= config.minGapHeight) {
        gapRegions.push_back(SqueezeRegion{"anchor-gap-" + std::to_string(gapIdx++), curDocY,
                                           totalDocHeight, config.gapAlpha});
    }

    return gapRegions;
}

} // namespace FluidCore
