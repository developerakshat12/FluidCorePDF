#include "services/StrokeStabilizer.h"

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

int main() {
    testCentripetalCatmullRomToBezier();
    testVelocityAdaptiveDeadzone();
    testShortStrokes();
    testStreamingLifecycle();
    testPerceivedLatencyBenchmark();
    std::cout << "All StrokeStabilizer tests passed successfully!\n";
    return 0;
}
