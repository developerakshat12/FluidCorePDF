#pragma once

#include "FluidCoreAPI.h"
#include "workspace/ExcerptPayload.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace FluidCore {

class CardLayoutEngine {
  public:
    // Outer card container dimension calculator for text & visual diagram excerpts
    static std::pair<double, double>
    computeExcerptCardDimensions(const ExcerptDropPayload& payload);

    // Bounding box of [ ↗ Anchor ] pill button in screen coordinates
    static Rectangle getExcerptAnchorPillRect(const Rectangle& cardWorldBounds, double originX,
                                              double originY, double zoom);

    // Bounding box of Stack Header bar in screen coordinates
    static Rectangle getStackHeaderRect(const Rectangle& stackWorldBounds, double originX,
                                        double originY, double zoom);

    // Bounding box of Stack Chevron toggle button [▼]/[▶] in screen coordinates
    static Rectangle getStackChevronRect(const Rectangle& stackWorldBounds, double originX,
                                         double originY, double zoom);
};

} // namespace FluidCore
