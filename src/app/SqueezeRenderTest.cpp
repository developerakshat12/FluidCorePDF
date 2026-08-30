#include "SqueezeRenderHelper.h"
#include "squeeze/SqueezeEngine.h"
#include "undo/SqueezeCommands.h"
#include "undo/UndoStack.h"

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

void testPixelSeamContinuity() {
    std::cout << "Running testPixelSeamContinuity...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {
        {0, 612.0, 792.0, 0.0},
        {1, 612.0, 792.0, 804.0},
        {2, 612.0, 792.0, 1608.0},
        {3, 612.0, 792.0, 2412.0},
    };
    engine.registerDocumentGeometry("doc-seam", pages);
    engine.setSqueezeRegion("doc-seam", 200.0, 500.0, 0.3);
    engine.setSqueezeRegion("doc-seam", 750.0, 950.0, 0.1);
    engine.setSqueezeRegion("doc-seam", 1500.0, 2000.0, 0.5);

    const auto& segments = engine.getSegments("doc-seam");

    for (const auto& pg : pages) {
        auto slices = SqueezeRenderHelper::decomposePage(pg.pageIndex, pg.unscaledYOffset,
                                                         pg.heightPt, segments);
        check(!slices.empty(), "page has slices");
        for (std::size_t i = 0; i + 1 < slices.size(); ++i) {
            check(std::abs(slices[i].screenYEnd - slices[i + 1].screenYStart) < kEps,
                  "screen Y continuity between adjacent slices");
            check(std::abs(slices[i].globalDocYEnd - slices[i + 1].globalDocYStart) < kEps,
                  "global doc Y continuity between adjacent slices");
            check(std::abs(slices[i].pageLocalDocYEnd - slices[i + 1].pageLocalDocYStart) < kEps,
                  "local doc Y continuity between adjacent slices");
        }
    }
}

void testCrossCreaseStrokeSubdivision() {
    std::cout << "Running testCrossCreaseStrokeSubdivision...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-stroke", pages);
    engine.setSqueezeRegion("doc-stroke", 200.0, 400.0, 0.2);

    const auto& segments = engine.getSegments("doc-stroke");
    auto bps = SqueezeRenderHelper::extractBreakpoints(segments);

    // Diagonal stroke from (0, 100) to (200, 500)
    Point p1{0.0, 100.0};
    Point p2{200.0, 500.0};

    auto subdivided = SqueezeRenderHelper::subdividePointSpan(p1, p2, bps);
    check(subdivided.size() == 4, "diagonal stroke subdivided into 4 points (3 spans)");

    check(std::abs(subdivided[0].x - 0.0) < kEps && std::abs(subdivided[0].y - 100.0) < kEps,
          "p1 matches");
    check(std::abs(subdivided[1].y - 200.0) < kEps, "first split at Y=200");
    check(std::abs(subdivided[1].x - 50.0) < kEps, "first split X=50");
    check(std::abs(subdivided[2].y - 400.0) < kEps, "second split at Y=400");
    check(std::abs(subdivided[2].x - 150.0) < kEps, "second split X=150");
    check(std::abs(subdivided[3].x - 200.0) < kEps && std::abs(subdivided[3].y - 500.0) < kEps,
          "p2 matches");
}

void testSelectionRectSubdivision() {
    std::cout << "Running testSelectionRectSubdivision...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-rect", pages);
    engine.setSqueezeRegion("doc-rect", 300.0, 600.0, 0.25);

    const auto& segments = engine.getSegments("doc-rect");
    auto bps = SqueezeRenderHelper::extractBreakpoints(segments);

    // Selection rect spanning Y=200 to Y=800 (height 600)
    Rectangle selRect{50.0, 200.0, 400.0, 600.0};
    auto subRects = SqueezeRenderHelper::subdivideRect(selRect, bps);

    check(subRects.size() == 3, "selection rect subdivided into 3 sub-rectangles");
    check(std::abs(subRects[0].y - 200.0) < kEps && std::abs(subRects[0].h - 100.0) < kEps,
          "subrect 1: [200, 300]");
    check(std::abs(subRects[1].y - 300.0) < kEps && std::abs(subRects[1].h - 300.0) < kEps,
          "subrect 2: [300, 600]");
    check(std::abs(subRects[2].y - 600.0) < kEps && std::abs(subRects[2].h - 200.0) < kEps,
          "subrect 3: [600, 800]");
}

void testLivePreviewVsCommit() {
    std::cout << "Running testLivePreviewVsCommit...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-prev", pages);
    engine.setSqueezeRegionWithId("doc-prev", "reg-1", 100.0, 200.0, 0.5);

    check(engine.getRawRegions("doc-prev").size() == 1, "1 raw region");

    // Live preview during drag
    engine.setPreviewSqueezeRegion("doc-prev", 400.0, 600.0, 0.2);
    check(engine.getRawRegions("doc-prev").size() == 1, "raw regions untouched by preview");
    check(engine.getSegments("doc-prev").size() > 3, "segments reflect preview region");

    // Clear preview
    engine.clearPreviewSqueezeRegion("doc-prev");
    check(engine.getRawRegions("doc-prev").size() == 1, "raw regions still intact");

    // Commit new region
    engine.setSqueezeRegionWithId("doc-prev", "reg-2", 400.0, 600.0, 0.2);
    check(engine.getRawRegions("doc-prev").size() == 2, "2 raw regions after commit");
}

void testSqueezeUndoRedo() {
    std::cout << "Running testSqueezeUndoRedo...\n";
    SqueezeEngine engine;
    std::vector<PageGeometry> pages = {{0, 612.0, 1000.0, 0.0}};
    engine.registerDocumentGeometry("doc-undo", pages);
    UndoStack stack;

    const double initialHeight = engine.totalSqueezedHeight("doc-undo");
    check(std::abs(initialHeight - 1000.0) < kEps, "initial unsqueezed height");

    // Apply squeeze fold via command
    std::vector<SqueezeRegion> folds = {{"fold-1", 200.0, 600.0, 0.25}};
    stack.pushAndExecute(std::make_unique<SetSqueezeRegionsCommand>(engine, "doc-undo", folds));

    const double squeezedHeight = engine.totalSqueezedHeight("doc-undo");
    check(squeezedHeight < initialHeight, "height decreased after fold");
    check(std::abs(squeezedHeight - 700.0) < kEps,
          "squeezed height is exactly 700 (400 compressed to 100)");

    // Undo fold
    stack.undo();
    check(std::abs(engine.totalSqueezedHeight("doc-undo") - 1000.0) < kEps,
          "height restored to 1000 after undo");

    // Redo fold
    stack.redo();
    check(std::abs(engine.totalSqueezedHeight("doc-undo") - 700.0) < kEps,
          "height compressed to 700 after redo");

    // Reset fold via command
    stack.pushAndExecute(std::make_unique<ResetSqueezeCommand>(engine, "doc-undo"));
    check(std::abs(engine.totalSqueezedHeight("doc-undo") - 1000.0) < kEps,
          "height reset to 1000 after reset command");

    stack.undo();
    check(std::abs(engine.totalSqueezedHeight("doc-undo") - 700.0) < kEps,
          "height restored to 700 after undo of reset");
}

} // namespace

int main() {
    testPixelSeamContinuity();
    testCrossCreaseStrokeSubdivision();
    testSelectionRectSubdivision();
    testLivePreviewVsCommit();
    testSqueezeUndoRedo();

    std::cout << "All SqueezeRenderTest suites passed successfully!\n";
    return 0;
}
