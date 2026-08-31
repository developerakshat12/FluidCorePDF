#include "workspace/WorkspaceInteraction.h"
#include "FluidCoreEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"

#include <cmath>
#include <iostream>

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

int testCoordinateTransforms() {
    int failed = 0;
    WorkspaceState state;
    state.viewport.originX = 100.0;
    state.viewport.originY = 200.0;
    state.viewport.zoom = 2.0;

    Point worldPt{150.0, 250.0};
    Point screenPt = state.viewport.worldToScreen(worldPt.x, worldPt.y);
    failed += check(screenPt.x == 100.0, "worldToScreen X is (150-100)*2 = 100");
    failed += check(screenPt.y == 100.0, "worldToScreen Y is (250-200)*2 = 100");

    Point roundTripWorld = state.viewport.screenToWorld(screenPt.x, screenPt.y);
    failed += check(roundTripWorld.x == 150.0, "screenToWorld roundtrip X matches");
    failed += check(roundTripWorld.y == 250.0, "screenToWorld roundtrip Y matches");

    return failed;
}

int testMinimapHitTestAndRect() {
    int failed = 0;
    WorkspaceState state;
    state.showMinimap = true;
    state.minimapWidth = 200.0;
    state.minimapHeight = 140.0;
    state.minimapMargin = 16.0;

    int viewW = 1000;
    int viewH = 800;

    Rectangle mm = WorkspaceInteraction::getMinimapRect(state, viewW, viewH);
    failed += check(mm.w == 200.0, "Minimap width is 200");
    failed += check(mm.h == 140.0, "Minimap height is 140");
    failed += check(mm.x == 1000.0 - 200.0 - 16.0, "Minimap right aligned with 16pt margin");
    failed += check(mm.y == 800.0 - 140.0 - 16.0, "Minimap bottom aligned with 16pt margin");

    failed += check(WorkspaceInteraction::minimapHitTest(state, mm.x + 10, mm.y + 10, viewW, viewH),
                    "Hit test inside minimap returns true");
    failed += check(!WorkspaceInteraction::minimapHitTest(state, 50, 50, viewW, viewH),
                    "Hit test outside minimap returns false");

    return failed;
}

int testSpatialHitTesting() {
    int failed = 0;
    FluidCoreEngine engine("test_project");

    auto card = std::make_unique<ExcerptCardNode>("card-1", Rectangle{100.0, 100.0, 200.0, 100.0},
                                                  "doc-1", 0, Rectangle{0, 0, 1, 1}, "Snippet",
                                                  false, Color{255, 255, 0, 255}, 1000);
    engine.insertNode(std::move(card));

    const auto* hit = WorkspaceInteraction::hitTestNodeAtWorldPoint(engine, Point{150.0, 150.0});
    failed += check(hit != nullptr, "hitTestNodeAtWorldPoint finds node");
    if (hit) {
        failed += check(hit->id() == "card-1", "hit node id matches");
    }

    const auto* miss = WorkspaceInteraction::hitTestNodeAtWorldPoint(engine, Point{500.0, 500.0});
    failed += check(miss == nullptr, "hitTestNodeAtWorldPoint returns null for empty space");

    return failed;
}

} // namespace

int main() {
    int failed = 0;
    failed += testCoordinateTransforms();
    failed += testMinimapHitTestAndRect();
    failed += testSpatialHitTesting();

    if (failed == 0) {
        std::cout << "All WorkspaceInteraction tests passed!\n";
    }
    return failed;
}
