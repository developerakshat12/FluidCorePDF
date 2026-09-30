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
    static constexpr double kCardAnchorWidth = 16.0;
    static constexpr double kCardHeaderHeight = 28.0;
    static constexpr double kCardMarginLeft = 8.0;
    static constexpr double kCardMarginRight = 8.0;
    static constexpr double kCardMarginTop = 6.0;
    static constexpr double kCardMarginBottom = 6.0;

    static constexpr double kTotalChromeWidth = 32.0;  // 16 anchor + 8 left + 8 right
    static constexpr double kTotalChromeHeight = 40.0; // 28 header + 6 top + 6 bottom

    // Outer card container dimension calculator for text & visual diagram excerpts
    static std::pair<double, double>
    computeExcerptCardDimensions(const ExcerptDropPayload& payload);

    // World-space box (points) used for tile rendering requests
    static Rectangle cardImageBodyWorldRect(const Rectangle& cardWorldBounds);

    // Screen-space box (pixels) used by WorkspaceRenderer and verification tests
    static Rectangle cardImageBodyScreenRect(const Rectangle& cardWorldBounds, double originX,
                                             double originY, double zoom);

    // Bounding box of left anchor bar / button in screen coordinates
    static Rectangle getExcerptAnchorRect(const Rectangle& cardWorldBounds, double originX,
                                          double originY, double zoom);

    // Backwards compatibility wrapper for getExcerptAnchorRect
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
