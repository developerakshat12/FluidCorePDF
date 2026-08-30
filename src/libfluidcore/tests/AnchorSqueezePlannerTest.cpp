#include "search/AnchorSqueezePlanner.h"

#include <cmath>
#include <iostream>
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

constexpr double kEps = 1e-6;

int testEmptyAnchors() {
    auto regions = AnchorSqueezePlanner::computeAnchorSqueezeRegions(1000.0, {});
    return check(regions.empty(), "Empty anchors produces 0 gap regions");
}

int testSingleAnchor() {
    int failures = 0;
    std::vector<AnchorSpan> anchors = {{400.0, 440.0, "highlight", 1}};
    AnchorSqueezeConfig config;
    config.contextPadding = 30.0;
    config.gapAlpha = 0.04;
    config.minGapHeight = 10.0;

    // Anchor [400, 440] -> uncollapsed [370, 470]
    // Gaps: [0, 370] and [470, 1000]
    auto regions = AnchorSqueezePlanner::computeAnchorSqueezeRegions(1000.0, anchors, config);
    failures += check(regions.size() == 2, "Single anchor generates 2 gap regions");
    if (regions.size() == 2) {
        failures += check(std::abs(regions[0].yStart - 0.0) < kEps &&
                          std::abs(regions[0].yEnd - 370.0) < kEps, "Gap 1 is [0, 370]");
        failures += check(std::abs(regions[0].alpha - 0.04) < kEps, "Gap 1 alpha is 0.04");
        failures += check(std::abs(regions[1].yStart - 470.0) < kEps &&
                          std::abs(regions[1].yEnd - 1000.0) < kEps, "Gap 2 is [470, 1000]");
    }
    return failures;
}

int testMultiSourceUnion() {
    int failures = 0;
    // Search hit at [200, 220], Highlight at [240, 260], Excerpt source at [600, 700]
    // With padding = 20.0:
    // [200, 220] -> [180, 240]
    // [240, 260] -> [220, 280] -> overlaps [180, 240]! Union is [180, 280]
    // [600, 700] -> [580, 720]
    std::vector<AnchorSpan> anchors = {
        {200.0, 220.0, "search", 10},
        {240.0, 260.0, "highlight", 5},
        {600.0, 700.0, "excerpt", 8}
    };
    AnchorSqueezeConfig config;
    config.contextPadding = 20.0;
    config.gapAlpha = 0.04;

    auto regions = AnchorSqueezePlanner::computeAnchorSqueezeRegions(1000.0, anchors, config);
    failures += check(regions.size() == 3, "Overlapping search+highlight union + excerpt -> 3 gaps");
    if (regions.size() == 3) {
        failures += check(std::abs(regions[0].yEnd - 180.0) < kEps, "Gap 1 ends at 180");
        failures += check(std::abs(regions[1].yStart - 280.0) < kEps &&
                          std::abs(regions[1].yEnd - 580.0) < kEps, "Gap 2 is [280, 580]");
        failures += check(std::abs(regions[2].yStart - 720.0) < kEps, "Gap 3 starts at 720");
    }
    return failures;
}

int testBoundaryClamping() {
    int failures = 0;
    // Anchor near top: [5, 20] (padded: [0, 40])
    // Anchor near bottom: [980, 995] (padded: [960, 1000])
    std::vector<AnchorSpan> anchors = {
        {5.0, 20.0, "cursor", 1},
        {980.0, 995.0, "cursor", 1}
    };
    AnchorSqueezeConfig config;
    config.contextPadding = 20.0;
    config.gapAlpha = 0.04;

    auto regions = AnchorSqueezePlanner::computeAnchorSqueezeRegions(1000.0, anchors, config);
    failures += check(regions.size() == 1, "Top and bottom boundary anchors leave 1 middle gap");
    if (regions.size() == 1) {
        failures += check(std::abs(regions[0].yStart - 40.0) < kEps &&
                          std::abs(regions[0].yEnd - 960.0) < kEps, "Middle gap is [40, 960]");
    }
    return failures;
}

int testContinuousAlphaModulation() {
    int failures = 0;
    std::vector<AnchorSpan> anchors = {{300.0, 350.0, "highlight", 1}};
    AnchorSqueezeConfig config;
    config.contextPadding = 20.0;

    // Test alpha = 0.50
    config.gapAlpha = 0.50;
    auto r1 = AnchorSqueezePlanner::computeAnchorSqueezeRegions(1000.0, anchors, config);
    failures += check(r1.size() == 2 && std::abs(r1[0].alpha - 0.50) < kEps, "Alpha 0.50 respected");

    // Test alpha = 0.04 (kMinAlpha)
    config.gapAlpha = 0.04;
    auto r2 = AnchorSqueezePlanner::computeAnchorSqueezeRegions(1000.0, anchors, config);
    failures += check(r2.size() == 2 && std::abs(r2[0].alpha - 0.04) < kEps, "Alpha 0.04 respected");

    return failures;
}

} // namespace

int main() {
    int totalFailures = 0;
    totalFailures += testEmptyAnchors();
    totalFailures += testSingleAnchor();
    totalFailures += testMultiSourceUnion();
    totalFailures += testBoundaryClamping();
    totalFailures += testContinuousAlphaModulation();

    if (totalFailures == 0) {
        std::cout << "AnchorSqueezePlannerTest: all 5 test suites passed successfully!\n";
        return 0;
    }
    std::cerr << "AnchorSqueezePlannerTest: " << totalFailures << " failure(s) detected\n";
    return 1;
}
