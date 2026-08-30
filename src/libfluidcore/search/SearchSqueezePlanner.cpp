#include "search/SearchSqueezePlanner.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace FluidCore {

std::vector<SqueezeRegion>
SearchSqueezePlanner::computeSearchSqueezeRegions(double totalDocHeight,
                                                  const std::vector<SearchHitSpan>& hits,
                                                  const SearchSqueezeConfig& config) {
    std::vector<SqueezeRegion> gapRegions;
    if (hits.empty() || totalDocHeight <= 0.0) {
        return gapRegions;
    }

    // Step 1: Pre-sort spans by docYStart to guarantee monotonic interval processing
    std::vector<SearchHitSpan> sortedHits = hits;
    std::sort(
        sortedHits.begin(), sortedHits.end(),
        [](const SearchHitSpan& a, const SearchHitSpan& b) { return a.docYStart < b.docYStart; });

    // Step 2: Expand each hit by context padding and clamp to document bounds [0, totalDocHeight]
    struct Interval {
        double y0;
        double y1;
    };
    std::vector<Interval> expanded;
    expanded.reserve(sortedHits.size());

    for (const auto& h : sortedHits) {
        const double hMin = std::min(h.docYStart, h.docYEnd);
        const double hMax = std::max(h.docYStart, h.docYEnd);
        const double y0 = std::clamp(hMin - config.contextPadding, 0.0, totalDocHeight);
        const double y1 = std::clamp(hMax + config.contextPadding, 0.0, totalDocHeight);
        if (y1 > y0) {
            expanded.push_back({y0, y1});
        }
    }

    if (expanded.empty()) {
        return gapRegions;
    }

    // Step 3: Perform continuous interval union on uncollapsed hit windows
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
            gapRegions.push_back(SqueezeRegion{"search-gap-" + std::to_string(gapIdx++), curDocY,
                                               m.y0, config.gapAlpha});
        }
        curDocY = m.y1;
    }

    if (totalDocHeight - curDocY >= config.minGapHeight) {
        gapRegions.push_back(SqueezeRegion{"search-gap-" + std::to_string(gapIdx++), curDocY,
                                           totalDocHeight, config.gapAlpha});
    }

    return gapRegions;
}

} // namespace FluidCore
