#pragma once

#include <algorithm>
#include <cmath>

namespace FluidCore {

/**
 * @brief ViewportZoomAnchor provides mathematical utilities for calculating
 * zoom scroll transitions in scrolled viewports such that the content under
 * a chosen focal point (either mouse cursor or viewport center) remains
 * anchored at the identical screen offset before and after the zoom operation.
 */
struct ViewportZoomAnchor {
    /**
     * @brief Computes the new scroll adjustment value after zoom.
     *
     * @param currentScroll Current scroll adjustment value in pixels (e.g. vadj->value).
     * @param canvasFocal   Focal coordinate in canvas/widget space (e.g. event->y or currentScroll
     * + viewportH / 2.0).
     * @param oldZoom       Previous zoom factor (> 0).
     * @param newZoom       Target zoom factor (> 0).
     * @param upper         Adjustment upper limit in pixels (optional, 0 to bypass clamping).
     * @param pageSize      Adjustment page size / visible dimension in pixels (optional).
     * @return Clamped new scroll adjustment value.
     */
    static double computeNewScroll(double currentScroll, double canvasFocal, double oldZoom,
                                   double newZoom, double upper = 0.0, double pageSize = 0.0) {
        if (oldZoom <= 1e-6) {
            return currentScroll;
        }

        const double zoomRatio = newZoom / oldZoom;
        const double newScroll = currentScroll + canvasFocal * (zoomRatio - 1.0);

        if (upper > 0.0 && pageSize > 0.0) {
            const double maxScroll = std::max(0.0, upper - pageSize);
            return std::clamp(newScroll, 0.0, maxScroll);
        }
        return std::max(0.0, newScroll);
    }
};

} // namespace FluidCore
