#pragma once

#include "FluidCoreAPI.h"

#include <cstddef>
#include <vector>

namespace FluidCore {

struct SearchHitSpan {
    double docYStart = 0.0;
    double docYEnd = 0.0;
};

struct SearchSqueezeConfig {
    double contextPadding = 40.0; // Context padding above and below each hit in document points
    double gapAlpha = 0.08;       // Canonical compression alpha for non-matching gaps
    double minGapHeight = 16.0;   // Minimum gap height worth squeezing
};

class SearchSqueezePlanner {
  public:
    // Computes gap SqueezeRegions by merging overlapping padded hit windows
    // and inverting into non-matching intervals over [0.0, totalDocHeight].
    static std::vector<SqueezeRegion>
    computeSearchSqueezeRegions(double totalDocHeight, const std::vector<SearchHitSpan>& hits,
                                const SearchSqueezeConfig& config = {});
};

} // namespace FluidCore
