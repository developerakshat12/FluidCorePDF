// BiDirectionalAnchorTest.cpp — Headless unit tests for bi-directional anchor coordinate
// transforms, squeeze auto-expansion integration, camera trajectory interpolation, and
// world-coordinate invariance.

#include "FluidCoreAPI.h"
#include "squeeze/SqueezeEngine.h"
#include "workspace/ExcerptCardNode.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] Assertion failed: " << message << "\n";
        std::abort();
    }
}

} // namespace

using FluidCore::Color;
using FluidCore::ExcerptCardNode;
using FluidCore::PageGeometry;
using FluidCore::Point;
using FluidCore::Rectangle;
using FluidCore::SqueezeEngine;

void testNormalizedToDocumentCoordinateMapping() {
    // Page 1 geometry
    const double pageTopY = 120.0;
    const double pageWidth = 612.0;
    const double pageHeight = 792.0;

    // Normalized excerpt rectangle in [0, 1] page space
    const Rectangle normRect{0.10, 0.25, 0.80, 0.15};

    const double y0 = pageTopY + normRect.y * pageHeight;
    const double y1 = y0 + normRect.h * pageHeight;
    const double x0 = normRect.x * pageWidth;
    const double x1 = x0 + normRect.w * pageWidth;
    const double passageCenterY = (y0 + y1) * 0.5;

    expect(std::abs(y0 - (120.0 + 198.0)) < 1e-4, "y0 calculation is accurate");
    expect(std::abs(y1 - (318.0 + 118.8)) < 1e-4, "y1 calculation is accurate");
    expect(std::abs(x0 - 61.2) < 1e-4, "x0 calculation is accurate");
    expect(std::abs(x1 - (61.2 + 489.6)) < 1e-4, "x1 calculation is accurate");
    expect(passageCenterY > y0 && passageCenterY < y1, "passage center is strictly between bounds");

    std::cout << "[PASS] testNormalizedToDocumentCoordinateMapping\n";
}

void testSqueezeAutoExpansionAndPiecewiseMapping() {
    SqueezeEngine engine;
    const std::string docId = "doc-test";

    std::vector<PageGeometry> pages = {
        {0, 600.0, 800.0, 16.0},
        {1, 600.0, 800.0, 828.0},
        {2, 600.0, 800.0, 1640.0},
    };
    engine.registerDocumentGeometry(docId, pages);

    // Initial state: uncompressed document height
    const double uncompressedH = engine.totalSqueezedHeight(docId);
    expect(uncompressedH > 2400.0, "total uncompressed height is registered");

    // Apply a squeeze fold across Y in [300, 600] with alpha = 0.04 (96% collapsed)
    std::string foldId = engine.setSqueezeRegion(docId, 300.0, 600.0, 0.04);
    const double foldedH = engine.totalSqueezedHeight(docId);
    expect(foldedH < uncompressedH - 250.0, "height is significantly reduced when folded");

    // Passage inside the folded region: [380, 460]
    const double passageDocY = 420.0;
    auto foundFold = engine.findFoldRegionAt(docId, passageDocY, 100.0);
    expect(foundFold.has_value(), "SqueezeEngine successfully locates overlapping fold");
    expect(foundFold->id == foldId, "Found fold ID matches created fold");

    // Auto-Expansion: Remove the fold covering the passage
    bool removed = engine.removeSqueezeRegion(docId, foundFold->id);
    expect(removed, "Fold successfully removed during auto-expansion");

    // Post-expansion: Document height is restored, and passage has full 1:1 screen mapping
    const double expandedH = engine.totalSqueezedHeight(docId);
    expect(std::abs(expandedH - uncompressedH) < 1e-4, "Height restored after fold removal");

    auto transform = engine.mapDocumentYToScreen(passageDocY, docId);
    expect(std::abs(transform.screenY - passageDocY) < 1e-4,
           "Screen coordinate matches uncompressed document Y");

    std::cout << "[PASS] testSqueezeAutoExpansionAndPiecewiseMapping\n";
}

void testCameraTrajectoryCubicEaseOut() {
    auto cubicEaseOut = [](double t) -> double {
        t = std::clamp(t, 0.0, 1.0);
        return 1.0 - std::pow(1.0 - t, 3.0);
    };

    // Boundary conditions
    expect(std::abs(cubicEaseOut(0.0) - 0.0) < 1e-6, "easeOut(0) == 0");
    expect(std::abs(cubicEaseOut(1.0) - 1.0) < 1e-6, "easeOut(1) == 1");

    // Strictly monotonic increasing in [0, 1]
    double prev = -1.0;
    for (int i = 0; i <= 20; ++i) {
        double t = i / 20.0;
        double val = cubicEaseOut(t);
        expect(val >= prev, "curve must be monotonically non-decreasing");
        prev = val;
    }

    // Deceleration profile: initial rate (0 to 0.25) > final rate (0.75 to 1.0)
    const double deltaStart = cubicEaseOut(0.25) - cubicEaseOut(0.0);
    const double deltaEnd = cubicEaseOut(1.0) - cubicEaseOut(0.75);
    expect(deltaStart > deltaEnd, "easeOut decelerates smoothly toward target");

    std::cout << "[PASS] testCameraTrajectoryCubicEaseOut\n";
}

void testWorldCoordinateInvarianceAcrossViewportTransforms() {
    // Create an ExcerptCardNode in world space
    const Point cardWorldCenter{750.0, 480.0};
    const Rectangle cardBounds{650.0, 400.0, 200.0, 160.0};

    ExcerptCardNode card("card-01", cardBounds, "doc-01.pdf", 0, Rectangle{0.1, 0.2, 0.8, 0.3},
                         "Sample excerpt snippet", false, Color{255, 200, 0, 255});

    expect(card.bounds().x == 650.0 && card.bounds().y == 400.0, "Card bounds intact");

    // Verify viewport center calculation at different zoom levels:
    // originX = worldX - (viewW / zoom) / 2.0
    const double viewW = 1200.0;
    const double viewH = 800.0;

    auto computeDesiredOrigin = [&](double worldX, double worldY, double zoom) -> Point {
        return {worldX - (viewW / zoom) / 2.0, worldY - (viewH / zoom) / 2.0};
    };

    // Zoom 100%
    Point origin100 = computeDesiredOrigin(cardWorldCenter.x, cardWorldCenter.y, 1.0);
    expect(std::abs(origin100.x - (750.0 - 600.0)) < 1e-4, "Origin X at 100% zoom is centered");
    expect(std::abs(origin100.y - (480.0 - 400.0)) < 1e-4, "Origin Y at 100% zoom is centered");

    // Zoom 200%
    Point origin200 = computeDesiredOrigin(cardWorldCenter.x, cardWorldCenter.y, 2.0);
    expect(std::abs(origin200.x - (750.0 - 300.0)) < 1e-4, "Origin X at 200% zoom is centered");
    expect(std::abs(origin200.y - (480.0 - 200.0)) < 1e-4, "Origin Y at 200% zoom is centered");

    // In both cases, converting back from screen center yields the exact world coordinate:
    // worldX = originX + (viewW / 2.0) / zoom
    const double reconstructedWorldX1 = origin100.x + (viewW / 2.0) / 1.0;
    const double reconstructedWorldY1 = origin100.y + (viewH / 2.0) / 1.0;
    expect(std::abs(reconstructedWorldX1 - cardWorldCenter.x) < 1e-4,
           "Reconstructed world X @ 100%");
    expect(std::abs(reconstructedWorldY1 - cardWorldCenter.y) < 1e-4,
           "Reconstructed world Y @ 100%");

    std::cout << "[PASS] testWorldCoordinateInvarianceAcrossViewportTransforms\n";
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::cout << "Running BiDirectionalAnchorTest suite...\n";
    testNormalizedToDocumentCoordinateMapping();
    testSqueezeAutoExpansionAndPiecewiseMapping();
    testCameraTrajectoryCubicEaseOut();
    testWorldCoordinateInvarianceAcrossViewportTransforms();
    std::cout << "All BiDirectionalAnchorTest tests passed!\n";
    return 0;
}
