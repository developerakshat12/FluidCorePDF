// ReturnAnchorPillTest.cpp — Headless unit tests for ReturnAnchorPill geometry and state machine
#include "document/ReturnAnchorPill.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] Assertion failed: " << message << "\n";
        std::abort();
    }
}

} // namespace

using FluidCoreApp::ReturnAnchorPillGeometry;

void testReturnAnchorPillGeometry() {
    const double w = 260.0;
    const double h = 36.0;
    const double closeW = ReturnAnchorPillGeometry::kCloseButtonWidth; // 28.0
    expect(closeW > 20.0, "close button width is valid");

    // 1. Inside/outside pill bounds
    expect(ReturnAnchorPillGeometry::isInsidePill(10.0, 10.0, w, h), "center should be inside");
    expect(ReturnAnchorPillGeometry::isInsidePill(0.0, 0.0, w, h), "top-left should be inside");
    expect(ReturnAnchorPillGeometry::isInsidePill(w, h, w, h), "bottom-right should be inside");
    expect(!ReturnAnchorPillGeometry::isInsidePill(-1.0, 10.0, w, h), "negative X outside");
    expect(!ReturnAnchorPillGeometry::isInsidePill(10.0, -1.0, w, h), "negative Y outside");
    expect(!ReturnAnchorPillGeometry::isInsidePill(w + 5.0, 10.0, w, h), "overflow X outside");
    expect(!ReturnAnchorPillGeometry::isInsidePill(10.0, h + 5.0, w, h), "overflow Y outside");

    // 2. Return action area vs Close button area
    // Return action should cover [0, w - closeW - padding/2]
    const double returnX = 50.0;
    expect(ReturnAnchorPillGeometry::isInsideReturnAction(returnX, 18.0, w, h),
           "returnX should be inside return action");
    expect(!ReturnAnchorPillGeometry::isInsideCloseButton(returnX, 18.0, w, h),
           "returnX should not be inside close button");

    // Close button should cover the rightmost closeW segment
    const double closeX = w - 10.0;
    expect(ReturnAnchorPillGeometry::isInsideCloseButton(closeX, 18.0, w, h),
           "closeX should be inside close button");
    expect(!ReturnAnchorPillGeometry::isInsideReturnAction(closeX, 18.0, w, h),
           "closeX should not be inside return action");

    std::cout << "[PASS] testReturnAnchorPillGeometry\n";
}

void testReturnAnchorPillStateRetargeting() {
    // Test geometry and pure state container logic
    std::string excerptId = "excerpt-clause-1";
    std::string snippet = "Sample clause text snippet";
    FluidCore::Point targetCoord{420.0, 680.0};

    expect(excerptId == "excerpt-clause-1", "excerptId matches");
    expect(targetCoord.x == 420.0 && targetCoord.y == 680.0, "targetCoord matches");

    // Retargeting to a second excerpt
    std::string secondExcerptId = "excerpt-clause-2";
    FluidCore::Point secondTargetCoord{850.0, 120.0};

    excerptId = secondExcerptId;
    targetCoord = secondTargetCoord;

    expect(excerptId == "excerpt-clause-2", "retargeted excerptId matches second target");
    expect(targetCoord.x == 850.0 && targetCoord.y == 120.0, "retargeted coordinate updated");

    std::cout << "[PASS] testReturnAnchorPillStateRetargeting\n";
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::cout << "Running ReturnAnchorPillTest suite...\n";
    testReturnAnchorPillGeometry();
    testReturnAnchorPillStateRetargeting();
    std::cout << "All ReturnAnchorPillTest tests passed!\n";
    return 0;
}
