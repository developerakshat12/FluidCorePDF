#include "squeeze/SqueezeEngine.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace FluidCore;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

bool close(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

// 1. No-op Transform: Without squeeze regions, Y_doc == Y_screen and alpha == 1.0.
int testNoOpTransform() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 792.0, 0.0}, {1, 612.0, 792.0, 800.0}};
    engine.registerDocumentGeometry("doc-noop", pages);

    auto res1 = engine.mapDocumentYToScreen(100.0, "doc-noop");
    failures += check(close(res1.screenY, 100.0), "No-op doc->screen maps 1:1");
    failures += check(close(res1.alpha, 1.0), "No-op doc->screen reports alpha=1.0");
    failures += check(res1.pageIndex == 0, "No-op pageIndex on page 0");

    auto res2 = engine.mapScreenYToDocument(850.0, "doc-noop");
    failures += check(close(res2.screenY, 850.0), "No-op screen->doc maps 1:1");
    failures += check(close(res2.alpha, 1.0), "No-op screen->doc reports alpha=1.0");
    failures += check(res2.pageIndex == 1, "No-op pageIndex on page 1");

    return failures;
}

// 2. Single Region Squeeze: Region [100, 300] with alpha = 0.5 compresses 200pt into 100pt.
int testSingleRegionSqueeze() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-single", pages);
    engine.setSqueezeRegion("doc-single", 100.0, 300.0, 0.5);

    // Query point after region: docY = 400.
    // Screen should be: 100 + (200 * 0.5) + (400 - 300) = 100 + 100 + 100 = 300.
    auto res = engine.mapDocumentYToScreen(400.0, "doc-single");
    failures +=
        check(close(res.screenY, 300.0), "Single region query after region compressed correctly");
    failures += check(close(res.alpha, 1.0), "Single region query after region reports alpha=1.0");

    // Inverse check: screenY = 300 -> docY = 400.
    auto inv = engine.mapScreenYToDocument(300.0, "doc-single");
    failures += check(close(inv.screenY, 400.0), "Single region inverse maps 300 back to 400");
    failures += check(close(inv.alpha, 1.0), "Single region inverse reports alpha=1.0");

    return failures;
}

// 3. Round-trip Accuracy: mapScreenYToDocument(mapDocumentYToScreen(Y)) == Y.
int testRoundTripAccuracy() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 2000.0, 0.0}};
    engine.registerDocumentGeometry("doc-roundtrip", pages);
    engine.setSqueezeRegion("doc-roundtrip", 200.0, 500.0, 0.3);
    engine.setSqueezeRegion("doc-roundtrip", 800.0, 1200.0, 0.6);

    std::vector<double> testDocYs = {0.0,   50.0,   200.0,  350.0,  500.0, 650.0,
                                     800.0, 1000.0, 1200.0, 1500.0, 2000.0};
    for (double docY : testDocYs) {
        auto forward = engine.mapDocumentYToScreen(docY, "doc-roundtrip");
        auto backward = engine.mapScreenYToDocument(forward.screenY, "doc-roundtrip");
        failures += check(close(backward.screenY, docY, 1e-6), "Round-trip preserves docY exactly");
    }

    return failures;
}

// 4. Overlapping Regions (Fully Nested): Outer [100, 500, 0.5], Inner [200, 400, 0.2].
int testNestedOverlappingRegions() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-nested", pages);
    engine.setSqueezeRegion("doc-nested", 100.0, 500.0, 0.5);
    engine.setSqueezeRegion("doc-nested", 200.0, 400.0, 0.2);

    // Inside inner region docY = 300:
    // docY 0..100: alpha 1.0 (len 100 -> screen 0..100)
    // docY 100..200: alpha 0.5 (len 100 -> screen 100..150)
    // docY 200..300: alpha 0.2 (len 100 -> screen 150..170)
    // expected screenY = 170.0, alpha = 0.2
    auto res = engine.mapDocumentYToScreen(300.0, "doc-nested");
    failures +=
        check(close(res.screenY, 170.0), "Nested overlap query inside inner region correct");
    failures += check(close(res.alpha, 0.2), "Nested overlap reports inner min alpha=0.2");

    // Outside inner region but inside outer region: docY = 450:
    // docY 200..400: alpha 0.2 (len 200 -> screen 150..190)
    // docY 400..450: alpha 0.5 (len 50 -> screen 190..215)
    // expected screenY = 215.0, alpha = 0.5
    auto res2 = engine.mapDocumentYToScreen(450.0, "doc-nested");
    failures += check(close(res2.screenY, 215.0), "Nested overlap query in outer region correct");
    failures += check(close(res2.alpha, 0.5), "Nested overlap reports outer alpha=0.5");

    return failures;
}

// 5. Page Index Resolution: Multi-page document with inter-page gaps.
int testPageIndexResolution() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {
        {0, 612.0, 792.0, 0.0}, {1, 612.0, 792.0, 850.0}, {2, 612.0, 792.0, 1700.0}};
    engine.registerDocumentGeometry("doc-pages", pages);
    engine.setSqueezeRegion("doc-pages", 400.0, 1000.0, 0.5);

    auto r0 = engine.mapDocumentYToScreen(200.0, "doc-pages");
    failures += check(r0.pageIndex == 0, "docY=200 resolves to page 0");

    auto r1 = engine.mapDocumentYToScreen(900.0, "doc-pages");
    failures += check(r1.pageIndex == 1, "docY=900 resolves to page 1");

    auto r2 = engine.mapDocumentYToScreen(1800.0, "doc-pages");
    failures += check(r2.pageIndex == 2, "docY=1800 resolves to page 2");

    // In gap between page 0 and page 1 (792..850) -> preceding page 0
    auto rGap = engine.mapDocumentYToScreen(820.0, "doc-pages");
    failures += check(rGap.pageIndex == 0, "Gap docY=820 resolves to page 0");

    return failures;
}

// 6. Partial Compression (Mid-Squeeze Query): docY landing inside squeeze region.
int testPartialCompressionMidSqueeze() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-mid", pages);
    engine.setSqueezeRegion("doc-mid", 100.0, 300.0, 0.5);

    // docY = 200 (midpoint of [100, 300])
    // screenY = 100 + (200 - 100) * 0.5 = 150.0, alpha = 0.5
    auto res = engine.mapDocumentYToScreen(200.0, "doc-mid");
    failures += check(close(res.screenY, 150.0), "Mid-squeeze query screenY is 150");
    failures += check(close(res.alpha, 0.5), "Mid-squeeze query alpha is 0.5");

    // Inverse check: screenY = 150 -> docY = 100 + (150 - 100) / 0.5 = 200
    auto inv = engine.mapScreenYToDocument(150.0, "doc-mid");
    failures += check(close(inv.screenY, 200.0), "Mid-squeeze inverse resolves to docY 200");
    failures += check(close(inv.alpha, 0.5), "Mid-squeeze inverse alpha is 0.5");

    return failures;
}

// 7. Degenerate Alpha Behavior: alpha <= 0 clamped to 0.04, ensuring invertibility.
int testDegenerateAlphaClamping() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-degen", pages);
    engine.setSqueezeRegion("doc-degen", 100.0, 300.0, 0.0); // 0.0 clamped to 0.04

    auto res = engine.mapDocumentYToScreen(200.0, "doc-degen");
    // screenY = 100 + (100 * 0.04) = 104.0
    failures += check(close(res.screenY, 104.0), "Alpha=0.0 clamped to 0.04, screenY is 104.0");
    failures += check(close(res.alpha, 0.04), "Alpha=0.0 clamped to 0.04 reports alpha=0.04");

    auto inv = engine.mapScreenYToDocument(104.0, "doc-degen");
    failures +=
        check(close(inv.screenY, 200.0), "Alpha=0.0 inverse correctly maps 104.0 back to 200.0");

    return failures;
}

// 8. Unregistered Document: Throws std::invalid_argument.
int testUnregisteredDocumentThrows() {
    int failures = 0;
    SqueezeEngine engine;

    bool threwDocToScreen = false;
    try {
        engine.mapDocumentYToScreen(100.0, "nonexistent");
    } catch (const std::invalid_argument&) {
        threwDocToScreen = true;
    }
    failures += check(threwDocToScreen, "mapDocumentYToScreen throws on unregistered docId");

    bool threwScreenToDoc = false;
    try {
        engine.mapScreenYToDocument(100.0, "nonexistent");
    } catch (const std::invalid_argument&) {
        threwScreenToDoc = true;
    }
    failures += check(threwScreenToDoc, "mapScreenYToDocument throws on unregistered docId");

    bool threwSetRegion = false;
    try {
        engine.setSqueezeRegion("nonexistent", 10.0, 20.0, 0.5);
    } catch (const std::invalid_argument&) {
        threwSetRegion = true;
    }
    failures += check(threwSetRegion, "setSqueezeRegion throws on unregistered docId");

    bool threwReset = false;
    try {
        engine.resetSqueeze("nonexistent");
    } catch (const std::invalid_argument&) {
        threwReset = true;
    }
    failures += check(threwReset, "resetSqueeze throws on unregistered docId");

    return failures;
}

// 9. Partial Overlap Merge: 3-segment outcome (left, middle-min-alpha, right).
int testPartialOverlapThreeSegmentMerge() {
    int failures = 0;
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-partial", pages);

    // Region 1: [100, 300] @ alpha 0.5
    // Region 2: [200, 400] @ alpha 0.8
    // Resulting segments:
    // 0..100: alpha 1.0 (len 100 -> screen 0..100)
    // 100..200: alpha 0.5 (len 100 -> screen 100..150)
    // 200..300: alpha min(0.5, 0.8) = 0.5 (len 100 -> screen 150..200) -> merged with 100..200
    // 300..400: alpha 0.8 (len 100 -> screen 200..280)
    // 400..1000: alpha 1.0 (len 600 -> screen 280..880)
    engine.setSqueezeRegion("doc-partial", 100.0, 300.0, 0.5);
    engine.setSqueezeRegion("doc-partial", 200.0, 400.0, 0.8);

    // Segment 1 (left): docY = 150 -> screenY = 125, alpha = 0.5
    auto r1 = engine.mapDocumentYToScreen(150.0, "doc-partial");
    failures += check(close(r1.screenY, 125.0), "Partial overlap left segment screenY=125");
    failures += check(close(r1.alpha, 0.5), "Partial overlap left segment alpha=0.5");

    // Segment 2 (middle intersection): docY = 250 -> screenY = 175, alpha = 0.5 (min of 0.5 and
    // 0.8)
    auto r2 = engine.mapDocumentYToScreen(250.0, "doc-partial");
    failures += check(close(r2.screenY, 175.0), "Partial overlap middle intersection screenY=175");
    failures += check(close(r2.alpha, 0.5), "Partial overlap middle intersection alpha=0.5");

    // Segment 3 (right): docY = 350 -> screenY = 200 + (50 * 0.8) = 240, alpha = 0.8
    auto r3 = engine.mapDocumentYToScreen(350.0, "doc-partial");
    failures += check(close(r3.screenY, 240.0), "Partial overlap right segment screenY=240");
    failures += check(close(r3.alpha, 0.8), "Partial overlap right segment alpha=0.8");

    // After regions: docY = 500 -> screenY = 280 + (100 * 1.0) = 380, alpha = 1.0
    auto r4 = engine.mapDocumentYToScreen(500.0, "doc-partial");
    failures += check(close(r4.screenY, 380.0), "Partial overlap after regions screenY=380");
    failures += check(close(r4.alpha, 1.0), "Partial overlap after regions alpha=1.0");

    // Inverse queries
    auto inv1 = engine.mapScreenYToDocument(125.0, "doc-partial");
    failures += check(close(inv1.screenY, 150.0), "Partial overlap inverse left maps to 150");

    auto inv2 = engine.mapScreenYToDocument(175.0, "doc-partial");
    failures += check(close(inv2.screenY, 250.0), "Partial overlap inverse middle maps to 250");

    auto inv3 = engine.mapScreenYToDocument(240.0, "doc-partial");
    failures += check(close(inv3.screenY, 350.0), "Partial overlap inverse right maps to 350");

    return failures;
}

} // namespace

int main() {
    int totalFailures = 0;

    totalFailures += testNoOpTransform();
    totalFailures += testSingleRegionSqueeze();
    totalFailures += testRoundTripAccuracy();
    totalFailures += testNestedOverlappingRegions();
    totalFailures += testPageIndexResolution();
    totalFailures += testPartialCompressionMidSqueeze();
    totalFailures += testDegenerateAlphaClamping();
    totalFailures += testUnregisteredDocumentThrows();
    totalFailures += testPartialOverlapThreeSegmentMerge();

    if (totalFailures == 0) {
        std::cout << "SqueezeEngineTest: all 9 test suites passed successfully!\n";
        return 0;
    }

    std::cerr << "SqueezeEngineTest: " << totalFailures << " failure(s) detected\n";
    return 1;
}
