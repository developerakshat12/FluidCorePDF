#pragma once

#include "FluidCoreAPI.h"

#include <cstddef>
#include <string>
#include <vector>

namespace FluidCore {

struct AnchorSpan {
    double docYStart = 0.0;
    double docYEnd = 0.0;
    std::string sourceTag; // e.g. "search", "highlight", "excerpt", "cursor"
    int priority = 0;
};

struct AnchorSqueezeConfig {
    double contextPadding = 32.0; // Context padding above and below anchor in document points
    double gapAlpha = 0.04;       // Canonical compression alpha for unanchored gaps (kMinAlpha)
    double minGapHeight = 16.0;   // Minimum gap height worth squeezing
};

class AnchorSqueezePlanner {
  public:
    // Computes gap SqueezeRegions by merging overlapping padded anchor windows
    // and inverting into unanchored intervals over [0.0, totalDocHeight].
    static std::vector<SqueezeRegion>
    computeAnchorSqueezeRegions(double totalDocHeight, const std::vector<AnchorSpan>& anchors,
                                const AnchorSqueezeConfig& config = {});
};

} // namespace FluidCore
