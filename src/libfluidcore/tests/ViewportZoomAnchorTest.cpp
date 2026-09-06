#include "geometry/ViewportZoomAnchor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << "\n";
        std::abort();
    }
}

void testCursorZoomInInvariance() {
    // Scenario: User viewing page at oldZoom 1.0, currentScroll = 5000.
    // Viewport height is 800. Mouse cursor is at 400px from top of viewport.
    // In canvas space: canvasFocal = 5000 + 400 = 5400.
    const double oldZoom = 1.0;
    const double newZoom = 2.0;
    const double oldScroll = 5000.0;
    const double viewportOffset = 400.0;
    const double canvasFocal = oldScroll + viewportOffset; // 5400.0

    const double newScroll = FluidCore::ViewportZoomAnchor::computeNewScroll(
        oldScroll, canvasFocal, oldZoom, newZoom);

    // Expected newScroll = 5000 + 5400 * (2.0 - 1.0) = 10400.0
    expect(std::abs(newScroll - 10400.0) < 1e-6, "Cursor zoom in scroll offset should be 10400");

    // Verify document coordinate under the cursor remains invariant:
    const double oldDocY = (oldScroll + viewportOffset) / oldZoom; // 5400.0
    const double newDocY = (newScroll + viewportOffset) / newZoom; // (10400 + 400) / 2.0 = 5400.0
    expect(std::abs(newDocY - oldDocY) < 1e-6, "Document coordinate under cursor must remain identical");
}

void testCursorZoomOutInvariance() {
    // Reverse scenario: oldZoom = 2.0, oldScroll = 10400.0, cursor at 400px in viewport.
    // canvasFocal = 10400 + 400 = 10800.0.
    const double oldZoom = 2.0;
    const double newZoom = 1.0;
    const double oldScroll = 10400.0;
    const double viewportOffset = 400.0;
    const double canvasFocal = oldScroll + viewportOffset; // 10800.0

    const double newScroll = FluidCore::ViewportZoomAnchor::computeNewScroll(
        oldScroll, canvasFocal, oldZoom, newZoom);

    // Expected newScroll = 10400 + 10800 * (0.5 - 1.0) = 5000.0
    expect(std::abs(newScroll - 5000.0) < 1e-6, "Cursor zoom out scroll offset should be 5000");

    const double oldDocY = (oldScroll + viewportOffset) / oldZoom;
    const double newDocY = (newScroll + viewportOffset) / newZoom;
    expect(std::abs(newDocY - oldDocY) < 1e-6, "Document coordinate under cursor must remain identical after zoom out");
}

void testCenterZoomInvariancePage87() {
    // Deep document scenario (simulating Page 87):
    // Document at scroll = 70000, viewport height = 800.
    // Toolbar zoom in by 1.2x (focal point = center of viewport).
    const double oldZoom = 1.0;
    const double newZoom = 1.2;
    const double oldScroll = 70000.0;
    const double viewportH = 800.0;
    const double canvasFocal = oldScroll + viewportH / 2.0; // 70400.0

    const double newScroll = FluidCore::ViewportZoomAnchor::computeNewScroll(
        oldScroll, canvasFocal, oldZoom, newZoom);

    // Expected newScroll = 70000 + 70400 * (1.2 - 1.0) = 84080.0
    expect(std::abs(newScroll - 84080.0) < 1e-6, "Page 87 center zoom should scale scroll to 84080");

    const double oldDocCenter = (oldScroll + viewportH / 2.0) / oldZoom; // 70400.0
    const double newDocCenter = (newScroll + viewportH / 2.0) / newZoom; // 84480 / 1.2 = 70400.0
    expect(std::abs(newDocCenter - oldDocCenter) < 1e-6, "Page 87 center must remain identical");
}

void testClampingBounds() {
    // Clamping at top boundary: zooming out near top should clamp to 0.0
    const double newScrollTop = FluidCore::ViewportZoomAnchor::computeNewScroll(
        100.0, 200.0, 1.0, 0.5, 10000.0, 800.0);
    // 100 + 200 * (0.5 - 1.0) = 0.0
    expect(newScrollTop == 0.0, "Zoom out near top must clamp to 0.0");

    // Clamping at bottom boundary: zooming in near bottom should clamp to upper - pageSize
    const double upper = 50000.0;
    const double pageSize = 1000.0;
    const double maxScroll = 49000.0;
    const double newScrollBottom = FluidCore::ViewportZoomAnchor::computeNewScroll(
        48500.0, 49000.0, 1.0, 2.0, upper, pageSize);
    // 48500 + 49000 * 1.0 = 97500 -> clamped to 49000
    expect(newScrollBottom == maxScroll, "Zoom in near bottom must clamp to upper - pageSize");
}

} // namespace

int main() {
    std::cout << "[ViewportZoomAnchorTest] Running zoom transformation invariance tests...\n";
    testCursorZoomInInvariance();
    testCursorZoomOutInvariance();
    testCenterZoomInvariancePage87();
    testClampingBounds();
    std::cout << "[ViewportZoomAnchorTest] All tests passed successfully.\n";
    return 0;
}
