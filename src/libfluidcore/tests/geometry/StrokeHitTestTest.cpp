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

void testStrokeBoundsAndWidth() {
    // Empty stroke
    FluidCore::Stroke emptyStroke;
    auto emptyBounds = FluidCore::computeStrokeBounds(emptyStroke);
    assert(emptyBounds.width == 0.0 && emptyBounds.height == 0.0);

    // Stroke with varying pressures
    FluidCore::Stroke stroke;
    stroke.id = "bounds-test";
    stroke.width = 4.0; // maxRenderedWidth with p=1.0: 4.0 * (0.25 + 0.75 * 1.0) = 4.0
    stroke.points = {
        {10.0, 20.0},
        {50.0, 80.0},
        {30.0, 40.0}
    };
    stroke.pressures = {0.2, 1.0};

    double maxW = FluidCore::maxRenderedStrokeWidth(stroke);
    assert(std::abs(maxW - 4.0) < 1e-5);

    double halfPad = maxW * 0.5 + 1.0; // 2.0 + 1.0 = 3.0
    auto bounds = FluidCore::computeStrokeBounds(stroke);
    // minX = 10, maxX = 50 -> [7.0, 53.0] -> width = 46.0
    // minY = 20, maxY = 80 -> [17.0, 83.0] -> height = 66.0
    assert(std::abs(bounds.x - 7.0) < 1e-4);
    assert(std::abs(bounds.y - 17.0) < 1e-4);
    assert(std::abs(bounds.width - 46.0) < 1e-4);
    assert(std::abs(bounds.height - 66.0) < 1e-4);

    std::cout << "  [PASS] testStrokeBoundsAndWidth\n";
}

void testRectangleIntersectionAndUnion() {
    FluidCore::Rectangle r1{10.0, 10.0, 50.0, 50.0}; // [10, 60] x [10, 60]
    FluidCore::Rectangle r2{40.0, 40.0, 50.0, 50.0}; // [40, 90] x [40, 90]
    FluidCore::Rectangle r3{70.0, 70.0, 20.0, 20.0}; // [70, 90] x [70, 90] - disjoint from r1
    FluidCore::Rectangle rEdge{60.0, 10.0, 20.0, 50.0}; // Touching r1 exactly on right border

    assert(FluidCore::rectanglesIntersect(r1, r2));
    assert(!FluidCore::rectanglesIntersect(r1, r3));
    assert(FluidCore::rectanglesIntersect(r2, r3));
    assert(FluidCore::rectanglesIntersect(r1, rEdge)); // Touching borders count as intersecting

    // Test uniteRectangles
    FluidCore::Rectangle emptyRect{0.0, 0.0, 0.0, 0.0};
    auto u1 = FluidCore::uniteRectangles(emptyRect, r1);
    assert(u1.x == r1.x && u1.y == r1.y && u1.width == r1.width && u1.height == r1.height);

    auto u2 = FluidCore::uniteRectangles(r1, r3);
    // x: min(10, 70) = 10, max(60, 90) = 90 -> width = 80
    // y: min(10, 70) = 10, max(60, 90) = 90 -> height = 80
    assert(std::abs(u2.x - 10.0) < 1e-4);
    assert(std::abs(u2.y - 10.0) < 1e-4);
    assert(std::abs(u2.width - 80.0) < 1e-4);
    assert(std::abs(u2.height - 80.0) < 1e-4);

    std::cout << "  [PASS] testRectangleIntersectionAndUnion\n";
}

int main() {
    std::cout << "=== Running StrokeHitTest Unit Tests ===\n";
    testPointToSegmentDistance();
    testDirectBugRegressionSparseStroke();
    testDirectHitOnStroke();
    testThresholdBoundary();
    testDegenerateTapStroke();
    testMultipleOverlappingStrokesSortedNearest();
    testStrokeBoundsAndWidth();
    testRectangleIntersectionAndUnion();
    std::cout << "=== All StrokeHitTest Tests Passed! ===\n";
    return 0;
}
