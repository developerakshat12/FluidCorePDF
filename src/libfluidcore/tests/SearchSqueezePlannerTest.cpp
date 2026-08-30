#include "search/SearchSqueezePlanner.h"
#include "app/SqueezeRenderHelper.h"
#include "squeeze/SqueezeEngine.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace FluidCore;
using namespace FluidCoreApp;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << "\n";
        return 1;
    }
    return 0;
}

constexpr double kEps = 1e-6;

void testEmptyHits() {
    std::cout << "Running testEmptyHits...\n";
    auto regions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, {});
    check(regions.empty(), "empty hits returns no squeeze regions");
}

void testSingleHit() {
    std::cout << "Running testSingleHit...\n";
    // Hit at [400, 420] with total height 1000.0, padding 40.0 -> uncollapsed [360, 460]
    // Gaps: [0, 360] and [460, 1000]
    std::vector<SearchHitSpan> hits = {{400.0, 420.0}};
    SearchSqueezeConfig config;
    config.contextPadding = 40.0;
    config.gapAlpha = 0.08;

    auto regions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits, config);
    check(regions.size() == 2, "single hit creates 2 gap regions");
    check(std::abs(regions[0].yStart - 0.0) < kEps && std::abs(regions[0].yEnd - 360.0) < kEps,
          "gap 1 is [0, 360]");
    check(std::abs(regions[0].alpha - 0.08) < kEps, "gap 1 alpha is 0.08");
    check(std::abs(regions[1].yStart - 460.0) < kEps && std::abs(regions[1].yEnd - 1000.0) < kEps,
          "gap 2 is [460, 1000]");
}

void testMultipleDistantHits() {
    std::cout << "Running testMultipleDistantHits...\n";
    // Hits at [200, 210] -> uncollapsed [160, 250]
    // Hits at [600, 610] -> uncollapsed [560, 650]
    std::vector<SearchHitSpan> hits = {{200.0, 210.0}, {600.0, 610.0}};
    auto regions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits);
    check(regions.size() == 3, "2 distant hits create 3 gap regions");
    check(std::abs(regions[0].yEnd - 160.0) < kEps, "gap 1 ends at 160");
    check(std::abs(regions[1].yStart - 250.0) < kEps && std::abs(regions[1].yEnd - 560.0) < kEps,
          "gap 2 is [250, 560]");
    check(std::abs(regions[2].yStart - 650.0) < kEps && std::abs(regions[2].yEnd - 1000.0) < kEps,
          "gap 3 is [650, 1000]");
}

void testProximateHitMerge() {
    std::cout << "Running testProximateHitMerge...\n";
    // Hit 1: [200, 220] (padded: [160, 260])
    // Hit 2: [280, 300] (padded: [240, 340])
    // Since 240 <= 260, they merge into uncollapsed [160, 340]!
    std::vector<SearchHitSpan> hits = {{200.0, 220.0}, {280.0, 300.0}};
    auto regions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits);
    check(regions.size() == 2, "proximate hits merged -> 2 gap regions (before and after)");
    check(std::abs(regions[0].yEnd - 160.0) < kEps, "gap 1 ends at 160");
    check(std::abs(regions[1].yStart - 340.0) < kEps, "gap 2 starts at 340");
}

void testBoundaryHitClamping() {
    std::cout << "Running testBoundaryHitClamping...\n";
    // Hit near top: [10, 30] (padded: [-30, 70] -> clamped: [0, 70])
    // Hit near bottom: [980, 995] (padded: [940, 1035] -> clamped: [940, 1000])
    std::vector<SearchHitSpan> hits = {{10.0, 30.0}, {980.0, 995.0}};
    auto regions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits);
    // Since hit 1 covers Y=0, no top gap!
    // Since hit 2 covers Y=1000, no bottom gap!
    // Only 1 intermediate gap: [70, 940]
    check(regions.size() == 1, "boundary hits at top & bottom leave 1 middle gap region");
    check(std::abs(regions[0].yStart - 70.0) < kEps && std::abs(regions[0].yEnd - 940.0) < kEps,
          "middle gap is [70, 940]");
}

void testManualVsSearchRegionIsolation() {
    std::cout << "Running testManualVsSearchRegionIsolation...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-iso", pages);

    // 1. User sets a manual fold at [200, 600, alpha=0.3]
    engine.setSqueezeRegionWithId("doc-iso", "user-fold", 200.0, 600.0, 0.3);
    check(std::abs(engine.mapDocumentYToScreen(400.0, "doc-iso").alpha - 0.3) < kEps,
          "manual fold alpha is 0.3 at Y=400");

    // 2. Search match occurs at Y=400 (inside the manual fold)
    std::vector<SearchHitSpan> hits = {{390.0, 410.0}}; // uncollapsed [350, 450]
    auto searchRegions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits);
    engine.setSearchSqueezeRegions("doc-iso", searchRegions);

    check(engine.isSearchSqueezeActive("doc-iso"), "search squeeze active");
    // The search match at Y=400 MUST resolve to alpha=1.0 uncollapsed!
    auto resMatch = engine.mapDocumentYToScreen(400.0, "doc-iso");
    check(std::abs(resMatch.alpha - 1.0) < kEps,
          "search match resolves to alpha=1.0 (uncollapsed)");

    // The gap before Y=350 MUST resolve to alpha=0.08
    auto resGap = engine.mapDocumentYToScreen(100.0, "doc-iso");
    check(std::abs(resGap.alpha - 0.08) < kEps, "gap resolves to alpha=0.08");
}

void testSnapshotAndRestoreOnClose() {
    std::cout << "Running testSnapshotAndRestoreOnClose...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-snap", pages);

    // Set manual fold
    engine.setSqueezeRegionWithId("doc-snap", "user-fold-1", 100.0, 300.0, 0.25);
    const double manualSqueezedHeight = engine.totalSqueezedHeight("doc-snap");
    check(std::abs(manualSqueezedHeight - 850.0) < kEps, "manual height is 850");

    // Activate search squeeze
    std::vector<SearchHitSpan> hits = {{500.0, 520.0}};
    auto searchRegions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits);
    engine.setSearchSqueezeRegions("doc-snap", searchRegions);
    check(engine.isSearchSqueezeActive("doc-snap"), "search active");

    // Close search squeeze (Escape)
    engine.clearSearchSqueeze("doc-snap");
    check(!engine.isSearchSqueezeActive("doc-snap"), "search inactive");
    check(std::abs(engine.totalSqueezedHeight("doc-snap") - 850.0) < kEps,
          "manual fold verbatim restored on search clear");
    check(engine.getRawRegions("doc-snap").size() == 1, "raw regions preserved");
    check(engine.getRawRegions("doc-snap")[0].id == "user-fold-1", "user fold ID preserved");
}

void testChainedMultiHitUnion() {
    std::cout << "Running testChainedMultiHitUnion...\n";
    // 3 overlapping hit windows:
    // Hit 1: [100, 120] -> [60, 160]
    // Hit 2: [150, 170] -> [110, 210]
    // Hit 3: [200, 220] -> [160, 260]
    // All 3 chain together into one interval: [60, 260]!
    std::vector<SearchHitSpan> hits = {{100.0, 120.0}, {150.0, 170.0}, {200.0, 220.0}};
    auto regions = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hits);
    check(regions.size() == 2, "3 chained hits merge into single uncollapsed window -> 2 gaps");
    check(std::abs(regions[0].yEnd - 60.0) < kEps, "gap 1 ends at 60");
    check(std::abs(regions[1].yStart - 260.0) < kEps, "gap 2 starts at 260");
}

void testOutOfOrderIngestion() {
    std::cout << "Running testOutOfOrderIngestion...\n";
    // Feed hits in reverse / random order
    std::vector<SearchHitSpan> hitsUnsorted = {{700.0, 720.0}, {100.0, 120.0}, {400.0, 420.0}};
    std::vector<SearchHitSpan> hitsSorted = {{100.0, 120.0}, {400.0, 420.0}, {700.0, 720.0}};

    auto regions1 = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hitsUnsorted);
    auto regions2 = SearchSqueezePlanner::computeSearchSqueezeRegions(1000.0, hitsSorted);

    check(regions1.size() == regions2.size(), "same region count");
    for (std::size_t i = 0; i < regions1.size(); ++i) {
        check(std::abs(regions1[i].yStart - regions2[i].yStart) < kEps, "yStart match");
        check(std::abs(regions1[i].yEnd - regions2[i].yEnd) < kEps, "yEnd match");
    }
}

void testCrossCreaseHighlightSubdivision() {
    std::cout << "Running testCrossCreaseHighlightSubdivision...\n";
    // Hit bounding box that spans across a fold boundary
    Rectangle hitBox{20.0, 180.0, 300.0, 50.0}; // [180, 230]
    std::vector<double> breakpoints = {200.0};  // Crease boundary at Y=200

    auto subBoxes = SqueezeRenderHelper::subdivideRect(hitBox, breakpoints);
    check(subBoxes.size() == 2, "search hit box subdivided into 2 rectangles at crease");
    check(std::abs(subBoxes[0].y - 180.0) < kEps && std::abs(subBoxes[0].h - 20.0) < kEps,
          "box 1: [180, 200]");
    check(std::abs(subBoxes[1].y - 200.0) < kEps && std::abs(subBoxes[1].h - 30.0) < kEps,
          "box 2: [200, 230]");
}

} // namespace

int main() {
    testEmptyHits();
    testSingleHit();
    testMultipleDistantHits();
    testProximateHitMerge();
    testBoundaryHitClamping();
    testManualVsSearchRegionIsolation();
    testSnapshotAndRestoreOnClose();
    testChainedMultiHitUnion();
    testOutOfOrderIngestion();
    testCrossCreaseHighlightSubdivision();

    std::cout << "All SearchSqueezePlanner tests passed successfully!\n";
    return 0;
}
