#include "geometry/StrokeHitTest.h"

#include <cassert>
#include <cmath>
#include <iostream>

void testPointToSegmentDistance() {
    // Normal projection onto interior of segment
    double d1 = FluidCore::pointToSegmentDistance(10.0, 5.0, 0.0, 0.0, 20.0, 0.0);
    assert(std::abs(d1 - 5.0) < 1e-6);

    // Clamped to start endpoint
    double d2 = FluidCore::pointToSegmentDistance(-10.0, 0.0, 0.0, 0.0, 20.0, 0.0);
    assert(std::abs(d2 - 10.0) < 1e-6);

    // Clamped to end endpoint
    double d3 = FluidCore::pointToSegmentDistance(30.0, 0.0, 0.0, 0.0, 20.0, 0.0);
    assert(std::abs(d3 - 10.0) < 1e-6);

    // Degenerate zero-length segment
    double d4 = FluidCore::pointToSegmentDistance(3.0, 4.0, 0.0, 0.0, 0.0, 0.0);
    assert(std::abs(d4 - 5.0) < 1e-6);

    std::cout << "  [PASS] testPointToSegmentDistance\n";
}

void testDirectBugRegressionSparseStroke() {
    // Synthetic stroke shaped like an 'L' with a diagonal return, leaving a huge empty interior
    // Points: (0, 0) -> (0, 300) -> (300, 300) -> (300, 250)
    // Bounding box: [0, 0, 300, 300]
    FluidCore::Stroke stroke;
    stroke.id = "sparse-loop-stroke";
    stroke.width = 2.0;
    stroke.points = {{0.0, 0.0}, {0.0, 300.0}, {300.0, 300.0}, {300.0, 250.0}};

    const double eraserRadius = 20.0;

    // Point (150, 100) is deep inside the stroke's bounding box [0, 0, 300, 300],
    // but > 150px away from the nearest ink segment!
    // Under the old bbox-only test, this was a false positive deletion.
    auto resultInterior = FluidCore::testPointAgainstStroke(150.0, 100.0, stroke, eraserRadius);
    assert(!resultInterior.hit);
    assert(resultInterior.distance > 100.0);

    // Another point in empty space inside bbox
    auto resultEmpty = FluidCore::testPointAgainstStroke(200.0, 50.0, stroke, eraserRadius);
    assert(!resultEmpty.hit);

    std::cout << "  [PASS] testDirectBugRegressionSparseStroke\n";
}

void testDirectHitOnStroke() {
    FluidCore::Stroke stroke;
    stroke.id = "straight-line";
    stroke.width = 4.0;
    stroke.points = {{10.0, 10.0}, {100.0, 10.0}};

    auto res = FluidCore::testPointAgainstStroke(50.0, 10.0, stroke, 15.0);
    assert(res.hit);
    assert(std::abs(res.distance - 0.0) < 1e-6);

    std::cout << "  [PASS] testDirectHitOnStroke\n";
}

void testThresholdBoundary() {
    FluidCore::Stroke stroke;
    stroke.id = "boundary-test";
    stroke.width = 6.0; // half-width = 3.0
    stroke.points = {{0.0, 0.0}, {0.0, 100.0}};

    const double eraserRadius = 20.0;
    // Effective threshold = 20.0 + 3.0 = 23.0

    // Just inside threshold (distance = 22.9)
    auto hitInside = FluidCore::testPointAgainstStroke(22.9, 50.0, stroke, eraserRadius);
    assert(hitInside.hit);
    assert(std::abs(hitInside.distance - 22.9) < 1e-4);

    // Just outside threshold (distance = 23.1, rejected by broad-phase AABB)
    auto missOutside = FluidCore::testPointAgainstStroke(23.1, 50.0, stroke, eraserRadius);
    assert(!missOutside.hit);

    // Point inside expanded AABB but outside threshold: (20.0, 115.0)
    // Nearest endpoint is (0, 100). Distance = sqrt(20^2 + 15^2) = 25.0 > 23.0
    auto missNarrow = FluidCore::testPointAgainstStroke(20.0, 115.0, stroke, eraserRadius);
    assert(!missNarrow.hit);
    assert(std::abs(missNarrow.distance - 25.0) < 1e-4);

    std::cout << "  [PASS] testThresholdBoundary\n";
}

void testDegenerateTapStroke() {
    FluidCore::Stroke stroke;
    stroke.id = "tap-stroke";
    stroke.width = 4.0; // half-width = 2.0
    stroke.points = {{50.0, 50.0}};

    const double eraserRadius = 15.0; // threshold = 17.0

    // Exactly on the point
    auto direct = FluidCore::testPointAgainstStroke(50.0, 50.0, stroke, eraserRadius);
    assert(direct.hit);
    assert(std::abs(direct.distance - 0.0) < 1e-6);

    // Within threshold (distance = 10.0)
    auto nearHit = FluidCore::testPointAgainstStroke(50.0, 60.0, stroke, eraserRadius);
    assert(nearHit.hit);
    assert(std::abs(nearHit.distance - 10.0) < 1e-6);

    // Outside threshold (distance = 25.0, rejected by broad-phase AABB)
    auto farMiss = FluidCore::testPointAgainstStroke(50.0, 75.0, stroke, eraserRadius);
    assert(!farMiss.hit);

    std::cout << "  [PASS] testDegenerateTapStroke\n";
}

void testMultipleOverlappingStrokesSortedNearest() {
    FluidCore::Stroke strokeH;
    strokeH.id = "stroke-horizontal";
    strokeH.width = 2.0;
    strokeH.points = {{0.0, 100.0}, {200.0, 100.0}};

    FluidCore::Stroke strokeV;
    strokeV.id = "stroke-vertical";
    strokeV.width = 2.0;
    strokeV.points = {{100.0, 0.0}, {100.0, 200.0}};

    std::vector<const FluidCore::Stroke*> candidates = {&strokeH, &strokeV};

    // Point at (104.0, 102.0)
    // Distance to strokeH (y=100) is 2.0
    // Distance to strokeV (x=100) is 4.0
    // Both are within eraserRadius = 15.0
    auto matches = FluidCore::findStrokesUnderPoint(104.0, 102.0, candidates, 15.0);
    assert(matches.size() == 2);
    // Nearest should be strokeH
    assert(matches[0].strokeId == "stroke-horizontal");
    assert(std::abs(matches[0].distance - 2.0) < 1e-4);
    // Second should be strokeV
    assert(matches[1].strokeId == "stroke-vertical");
    assert(std::abs(matches[1].distance - 4.0) < 1e-4);

    std::cout << "  [PASS] testMultipleOverlappingStrokesSortedNearest\n";
}

int main() {
    std::cout << "=== Running StrokeHitTest Unit Tests ===\n";
    testPointToSegmentDistance();
    testDirectBugRegressionSparseStroke();
    testDirectHitOnStroke();
    testThresholdBoundary();
    testDegenerateTapStroke();
    testMultipleOverlappingStrokesSortedNearest();
    std::cout << "=== All StrokeHitTest Tests Passed! ===\n";
    return 0;
}
