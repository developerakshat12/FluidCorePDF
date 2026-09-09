#include "services/StrokeStabilizer.h"
#include "services/CairoStrokeHelper.h"
#include <cairo.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << "\n";
        std::abort();
    }
}

} // namespace

using FluidCoreApp::StabilizerMode;
using FluidCoreApp::StrokeStabilizer;

void testCentripetalCatmullRomToBezier() {
    // 4 points on a horizontal line: P0(0,0), P1(10,0), P2(20,0), P3(30,0)
    StrokeStabilizer::Point2D p0 = {0.0, 0.0};
    StrokeStabilizer::Point2D p1 = {10.0, 0.0};
    StrokeStabilizer::Point2D p2 = {20.0, 0.0};
    StrokeStabilizer::Point2D p3 = {30.0, 0.0};

    auto seg = StrokeStabilizer::centripetalCatmullRomToBezier(p0, p1, p2, p3, 0.5, 0.8);
    expect(seg.p0 == p1, "B0 should equal P1");
    expect(seg.p3 == p2, "B3 should equal P2");
    expect(std::abs(seg.p1.y) < 1e-4, "B1.y should be 0 on horizontal line");
    expect(std::abs(seg.p2.y) < 1e-4, "B2.y should be 0 on horizontal line");
    expect(seg.p1.x > p1.x && seg.p1.x < p2.x, "B1.x should be between P1 and P2");
    expect(seg.p2.x > p1.x && seg.p2.x < p2.x, "B2.x should be between P1 and P2");
    expect(seg.pressure0 == 0.5, "pressure0 check");
    expect(seg.pressure1 == 0.8, "pressure1 check");

    // Sharp corner with varying distance to verify no cusps/wild overshoot
    StrokeStabilizer::Point2D c0 = {0.0, 100.0};
    StrokeStabilizer::Point2D c1 = {10.0, 0.0};
    StrokeStabilizer::Point2D c2 = {11.0, 0.0}; // Very close point
    StrokeStabilizer::Point2D c3 = {100.0, 0.0};
    auto sharpSeg = StrokeStabilizer::centripetalCatmullRomToBezier(c0, c1, c2, c3, 1.0, 1.0);
    expect(sharpSeg.p0 == c1, "sharp B0 == C1");
    expect(sharpSeg.p3 == c2, "sharp B3 == C2");
    // Centripetal parametrization keeps control points closely bounded
    expect(std::abs(sharpSeg.p1.x - c1.x) < 5.0, "centripetal B1 bounded");
    expect(std::abs(sharpSeg.p2.x - c2.x) < 5.0, "centripetal B2 bounded");

    std::cout << "[PASS] testCentripetalCatmullRomToBezier\n";
}

void testVelocityAdaptiveDeadzone() {
    StrokeStabilizer stabilizer;
    // Begin at (100, 100)
    stabilizer.beginStroke({100.0, 100.0}, 1.0, 1000, StabilizerMode::Smooth);

    // Micro-jitter at zero velocity (dt = 10ms, dist = 0.2px) -> should be filtered
    auto res1 = stabilizer.pushPoint({100.2, 100.0}, 1.0, 1010);
    expect(stabilizer.sampleCount() == 1, "micro-jitter should be filtered");

    // Duplicate timestamp (dt = 0ms) -> guard against divide-by-zero
    auto res2 = stabilizer.pushPoint({100.2, 100.0}, 1.0, 1010);
    expect(stabilizer.sampleCount() == 1, "zero dt should not crash");

    // Fast movement (dt = 8ms, dist = 20px -> v = 2.5 px/ms > threshold) -> bypasses deadzone
    auto res3 = stabilizer.pushPoint({120.0, 100.0}, 1.0, 1018);
    expect(stabilizer.sampleCount() == 2, "fast movement should pass");

    std::cout << "[PASS] testVelocityAdaptiveDeadzone\n";
}

void testShortStrokes() {
    // 1-point tap / dot
    {
        StrokeStabilizer stab;
        stab.beginStroke({50.0, 50.0}, 0.5, 100);
        auto finalSegs = stab.endStroke();
        expect(stab.sampleCount() == 1, "1 sample recorded");
        expect(finalSegs.empty(), "1-point stroke has 0 Bezier curves");
    }

    // 2-point short line
    {
        StrokeStabilizer stab;
        stab.beginStroke({0.0, 0.0}, 0.5, 100);
        stab.pushPoint({10.0, 10.0}, 0.7, 108);
        auto finalSegs = stab.endStroke();
        expect(stab.sampleCount() == 2, "2 samples recorded");
        expect(finalSegs.size() == 1, "2-point stroke emits 1 final segment");
    }

    // 3-point short curve
    {
        StrokeStabilizer stab;
        stab.beginStroke({0.0, 0.0}, 0.5, 100);
        stab.pushPoint({10.0, 0.0}, 0.6, 108);
        stab.pushPoint({20.0, 10.0}, 0.8, 116);
        auto finalSegs = stab.endStroke();
        expect(stab.sampleCount() == 3, "3 samples recorded");
        // Initial span committed mid-stroke (1) + tail flushed at end (1) = 2 total
        expect(finalSegs.size() >= 1, "3-point stroke flushes tail");
    }

    std::cout << "[PASS] testShortStrokes\n";
}

void testStreamingLifecycle() {
    StrokeStabilizer stab;
    stab.beginStroke({0.0, 0.0}, 1.0, 1000, StabilizerMode::Smooth);

    std::size_t totalCommitted = 0;
    // Push 10 points along a sine wave
    for (int i = 1; i <= 10; ++i) {
        double x = i * 10.0;
        double y = std::sin(i * 0.5) * 20.0;
        uint64_t t = 1000 + i * 8; // 125 Hz
        auto res = stab.pushPoint({x, y}, 1.0, t);
        totalCommitted += res.newlyCommitted.size();
        expect(res.hasWetSegment, "always has wet leading tip during active stroke");
    }

    auto tail = stab.endStroke();
    totalCommitted += tail.size();

    // 11 points total -> exactly 10 Bezier segments connecting them
    expect(totalCommitted == 10, "11 points must produce exactly 10 Bezier segments");
    std::cout << "[PASS] testStreamingLifecycle\n";
}

void testPerceivedLatencyBenchmark() {
    StrokeStabilizer stab;
    stab.beginStroke({0.0, 0.0}, 1.0, 0, StabilizerMode::Smooth);

    // Simulate 125 Hz stream (8ms delta) of 1,000 points
    const int kNumPoints = 1000;
    const double kInterSampleMs = 8.0;

    auto startTime = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= kNumPoints; ++i) {
        double x = i * 2.0;
        double y = i * 2.0;
        uint64_t t = static_cast<uint64_t>(i * kInterSampleMs);
        auto res = stab.pushPoint({x, y}, 1.0, t);

        // In Smooth mode, lookahead lag is strictly <= 1 sample (8ms)
        // When point i arrives (for i >= 4), segment [i-2, i-1] is committed
        if (i >= 4) {
            expect(!res.newlyCommitted.empty(), "must incrementally commit with <= 1 sample lag");
        }
    }
    auto endTime = std::chrono::high_resolution_clock::now();

    double totalComputeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    double perPointMicroseconds = (totalComputeMs / kNumPoints) * 1000.0;

    std::cout << "Latency Benchmark: " << kNumPoints << " points processed in " << totalComputeMs
              << " ms (" << perPointMicroseconds << " us/point)\n";

    // Per-event compute overhead must be < 50 microseconds (< 0.05ms)
    expect(perPointMicroseconds < 50.0, "compute overhead must be < 0.05ms per point");

    // Total perceived latency = 1 sample lag (8ms) + compute (< 0.05ms) <= 8.05ms << 20ms
    std::cout << "[PASS] testPerceivedLatencyBenchmark (perceived lag <= 8.05 ms <= 20 ms)\n";
}

void testStrokeBoundsHelper() {
    // 1. Empty stroke
    FluidCore::Stroke emptyStroke;
    auto emptyBounds = FluidCoreApp::computeStrokeClipBounds(emptyStroke);
    expect(!emptyBounds.valid, "Empty stroke must have invalid bounds");

    // 2. Completed stroke with points and stroke width
    FluidCore::Stroke stroke;
    stroke.width = 10.0;
    stroke.points = {{100.0, 100.0}, {200.0, 150.0}};
    // padding = (10.0 * 0.5) + 4.0 = 9.0
    auto b = FluidCoreApp::computeStrokeClipBounds(stroke, 4.0);
    expect(b.valid, "Stroke bounds must be valid");
    expect(std::abs(b.x - 91.0) < 1e-4, "b.x must be minX - pad (100 - 9 = 91)");
    expect(std::abs(b.y - 91.0) < 1e-4, "b.y must be minY - pad (100 - 9 = 91)");
    expect(std::abs(b.width - 118.0) < 1e-4, "b.width must be (maxX - minX) + 2*pad (100 + 18 = 118)");
    expect(std::abs(b.height - 68.0) < 1e-4, "b.height must be (maxY - minY) + 2*pad (50 + 18 = 68)");

    // 3. Active wet stroke with samples and wet tip
    std::vector<StrokeStabilizer::StabilizedSample> samples;
    samples.push_back({{50.0, 50.0}, 1.0, 0});
    samples.push_back({{100.0, 60.0}, 1.0, 10});
    // Wet tip is at (150.0, 70.0), beyond current committed samples
    StrokeStabilizer::Point2D wetTip = {150.0, 70.0};
    auto wetBounds = FluidCoreApp::computeWetStrokeClipBounds(samples, true, wetTip, 4.0, 2.0);
    // pad = (4.0 * 0.5) + 2.0 = 4.0
    // minX = 50, maxX = 150 -> x = 46, width = 108
    // minY = 50, maxY = 70 -> y = 46, height = 28
    expect(wetBounds.valid, "Wet bounds must be valid");
    expect(std::abs(wetBounds.x - 46.0) < 1e-4, "wetBounds.x must encompass minX");
    expect(std::abs((wetBounds.x + wetBounds.width) - 154.0) < 1e-4, "wetBounds must encompass wetTip.x");
    expect(std::abs((wetBounds.y + wetBounds.height) - 74.0) < 1e-4, "wetBounds must encompass wetTip.y");

    std::cout << "[PASS] testStrokeBoundsHelper (padding & wetTip inclusion verified)\n";
}

void testCairoPushGroupBoundedExtents() {
    // Simulate a full 1080p display buffer (1920 x 1080 = 8.29 MB)
    cairo_surface_t* windowSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
    cairo_t* cr = cairo_create(windowSurface);
    expect(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "Cairo context creation successful");

    // Case A: Unclipped push_group creates full 1920x1080 surface (8.29 MB)
    cairo_push_group(cr);
    cairo_surface_t* unclippedGroup = cairo_get_group_target(cr);
    int unclippedW = cairo_image_surface_get_width(unclippedGroup);
    int unclippedH = cairo_image_surface_get_height(unclippedGroup);
    expect(unclippedW == 1920 && unclippedH == 1080, "Unclipped group surface matches full window size (8.3 MB)");
    cairo_pop_group(cr);

    // Case B: Bounded clip to 300x100 rectangle creates strictly 300x100 group surface (~120 KB)
    cairo_save(cr);
    cairo_rectangle(cr, 200.0, 150.0, 300.0, 100.0);
    cairo_clip(cr);

    cairo_push_group(cr);
    cairo_surface_t* clippedGroup = cairo_get_group_target(cr);
    int clippedW = cairo_image_surface_get_width(clippedGroup);
    int clippedH = cairo_image_surface_get_height(clippedGroup);
    expect(clippedW == 300 && clippedH == 100, "Bounded group surface is strictly constrained to 300x100 (120 KB)");

    // Draw something into the group
    cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
    cairo_set_line_width(cr, 10.0);
    cairo_move_to(cr, 210.0, 200.0);
    cairo_line_to(cr, 490.0, 200.0);
    cairo_stroke(cr);

    // Composite back with alpha
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.45);
    expect(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "Cairo compositing succeeded");

    cairo_restore(cr);

    // Verify clip was restored on cr: extents should again cover the full window
    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    expect(x1 <= 0.0 && y1 <= 0.0 && x2 >= 1920.0 && y2 >= 1080.0, "Context clip restored cleanly to full window");

    // Case C: Scaled context (zoom = 2.0x)
    cairo_save(cr);
    cairo_scale(cr, 2.0, 2.0);
    cairo_rectangle(cr, 50.0, 50.0, 100.0, 50.0); // 100x50 in user coords -> 200x100 in device coords
    cairo_clip(cr);
    cairo_push_group(cr);
    cairo_surface_t* scaledGroup = cairo_get_group_target(cr);
    int scaledW = cairo_image_surface_get_width(scaledGroup);
    int scaledH = cairo_image_surface_get_height(scaledGroup);
    expect(scaledW == 200 && scaledH == 100, "Scaled group surface matches device-space clip extents (200x100)");
    cairo_pop_group(cr);
    cairo_restore(cr);

    // Case D: Viewport edge case — stroke crossing canvas boundary (starts at x=-50, y=100)
    cairo_save(cr);
    cairo_rectangle(cr, -50.0, 100.0, 250.0, 80.0);
    cairo_clip(cr);
    cairo_push_group(cr);
    cairo_surface_t* edgeGroup = cairo_get_group_target(cr);
    int edgeW = cairo_image_surface_get_width(edgeGroup);
    int edgeH = cairo_image_surface_get_height(edgeGroup);
    // Cairo automatically clamps group surface allocation to the valid intersection with the target device extents!
    expect(edgeW <= 200 && edgeH == 80, "Edge-crossing stroke clamps group to visible window without clipping error");
    cairo_set_source_rgb(cr, 1.0, 0.5, 0.0);
    cairo_set_line_width(cr, 12.0);
    cairo_move_to(cr, -40.0, 140.0);
    cairo_line_to(cr, 180.0, 140.0);
    cairo_stroke(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.45);
    expect(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "Edge-crossing stroke compositing succeeded");
    cairo_restore(cr);

    // Case E: Very thick highlighter (width = 30.0pt, pad = 19.0pt)
    cairo_save(cr);
    FluidCore::Stroke thickStroke;
    thickStroke.width = 30.0;
    thickStroke.points = {{300.0, 300.0}, {600.0, 400.0}};
    auto thickB = FluidCoreApp::computeStrokeClipBounds(thickStroke, 4.0);
    expect(thickB.valid, "Thick stroke bounds valid");
    // pad = (30 * 0.5) + 4 = 19.0
    expect(std::abs(thickB.x - (300.0 - 19.0)) < 1e-4, "Thick stroke padded minX");
    expect(std::abs(thickB.y - (300.0 - 19.0)) < 1e-4, "Thick stroke padded minY");
    expect(std::abs(thickB.width - (300.0 + 38.0)) < 1e-4, "Thick stroke width covers round caps");
    expect(std::abs(thickB.height - (100.0 + 38.0)) < 1e-4, "Thick stroke height covers round caps");
    cairo_rectangle(cr, thickB.x, thickB.y, thickB.width, thickB.height);
    cairo_clip(cr);
    cairo_push_group(cr);
    cairo_set_line_width(cr, thickStroke.width);
    cairo_move_to(cr, 300.0, 300.0);
    cairo_line_to(cr, 600.0, 400.0);
    cairo_stroke(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.45);
    expect(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "Thick stroke compositing succeeded");
    cairo_restore(cr);

    // Case F: Completely off-screen stroke (e.g. x = -500, y = -500)
    cairo_save(cr);
    cairo_rectangle(cr, -500.0, -500.0, 100.0, 100.0);
    cairo_clip(cr);
    cairo_push_group(cr);
    cairo_set_source_rgb(cr, 0.0, 1.0, 0.0);
    cairo_paint(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.45);
    expect(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "Completely offscreen stroke produces no error");
    cairo_restore(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(windowSurface);
    std::cout << "[PASS] testCairoPushGroupBoundedExtents (verified device-space group allocation & edge cases)\n";
}

int main() {
    testCentripetalCatmullRomToBezier();
    testVelocityAdaptiveDeadzone();
    testShortStrokes();
    testStreamingLifecycle();
    testPerceivedLatencyBenchmark();
    testStrokeBoundsHelper();
    testCairoPushGroupBoundedExtents();
    std::cout << "All StrokeStabilizer tests passed successfully!\n";
    return 0;
}
