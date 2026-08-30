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

    std::vector<AnchorSpan> anchors;
    anchors.reserve(hits.size());
    for (const auto& h : hits) {
        anchors.push_back(AnchorSpan{h.docYStart, h.docYEnd, "search", 10});
    }

    AnchorSqueezeConfig aConfig;
    aConfig.contextPadding = config.contextPadding;
    aConfig.gapAlpha = config.gapAlpha;
    aConfig.minGapHeight = config.minGapHeight;

    auto rawGaps = AnchorSqueezePlanner::computeAnchorSqueezeRegions(totalDocHeight, anchors, aConfig);

    std::size_t gapIdx = 1;
    for (auto& g : rawGaps) {
        g.id = "search-gap-" + std::to_string(gapIdx++);
        gapRegions.push_back(std::move(g));
    }

    return gapRegions;
}

} // namespace FluidCore
