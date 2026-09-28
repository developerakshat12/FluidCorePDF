#include "services/StylusButtonRouter.h"

#include <cstdlib>
#include <iostream>

// CTest runs the RelWithDebInfo configuration, which defines NDEBUG and therefore
// compiles assert() out entirely. A plain assert() would make every check below a
// no-op, so failures are reported explicitly instead.
#define REQUIRE(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "FAILED: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n";            \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using FluidCoreApp::StylusButtonRouter;

namespace {

// GDK's Win32 backend already delivers Wintab hardware as generic GDK input, so there is
// no Windows Ink / WinRT integration anywhere in the app. These tests pin the mapping
// that the GDK-level stylus support depends on.

void testDeviceSourceClassification() {
    REQUIRE(StylusButtonRouter::isStylusSource(GDK_SOURCE_PEN));
    REQUIRE(StylusButtonRouter::isStylusSource(GDK_SOURCE_ERASER));
    REQUIRE(!StylusButtonRouter::isStylusSource(GDK_SOURCE_MOUSE));
    REQUIRE(!StylusButtonRouter::isStylusSource(GDK_SOURCE_TOUCHSCREEN));

    // The eraser end of the stylus is a distinct GDK input source in GDK3 and does not
    // deliver barrel buttons, so the two must not be conflated.
    REQUIRE(StylusButtonRouter::isPenSource(GDK_SOURCE_PEN));
    REQUIRE(!StylusButtonRouter::isPenSource(GDK_SOURCE_ERASER));

    std::cout << "  [PASS] testDeviceSourceClassification\n";
}

void testBarrelButtonIsRecognizedOnPenOnly() {
    // GDK reports a pen barrel button as an ordinary middle or secondary button press
    // on the pen device.
    REQUIRE(StylusButtonRouter::isBarrelButton(GDK_BUTTON_MIDDLE, GDK_SOURCE_PEN));
    REQUIRE(StylusButtonRouter::isBarrelButton(GDK_BUTTON_SECONDARY, GDK_SOURCE_PEN));
    REQUIRE(!StylusButtonRouter::isBarrelButton(GDK_BUTTON_PRIMARY, GDK_SOURCE_PEN));

    // The eraser end has no barrel buttons.
    REQUIRE(!StylusButtonRouter::isBarrelButton(GDK_BUTTON_MIDDLE, GDK_SOURCE_ERASER));

    // Crucially, a real mouse's middle and right clicks must keep their own meaning,
    // otherwise every right-click in the app would start erasing.
    REQUIRE(!StylusButtonRouter::isBarrelButton(GDK_BUTTON_MIDDLE, GDK_SOURCE_MOUSE));
    REQUIRE(!StylusButtonRouter::isBarrelButton(GDK_BUTTON_SECONDARY, GDK_SOURCE_MOUSE));
    REQUIRE(!StylusButtonRouter::isBarrelButton(GDK_BUTTON_MIDDLE, GDK_SOURCE_TOUCHSCREEN));

    std::cout << "  [PASS] testBarrelButtonIsRecognizedOnPenOnly\n";
}

void testBarrelButtonClaimsEraserWithoutStealingMouseClicks() {
    using Intent = StylusButtonRouter::Intent;

    // Pen barrel button borrows the eraser.
    REQUIRE(StylusButtonRouter::classifyPress(GDK_BUTTON_MIDDLE, GDK_SOURCE_PEN) ==
            Intent::Erasing);
    REQUIRE(StylusButtonRouter::classifyPress(GDK_BUTTON_SECONDARY, GDK_SOURCE_PEN) ==
            Intent::Erasing);

    // The eraser end of the stylus is always erasing.
    REQUIRE(StylusButtonRouter::classifyPress(GDK_BUTTON_PRIMARY, GDK_SOURCE_ERASER) ==
            Intent::Erasing);

    // Mouse behaviour is preserved exactly: middle pans, right opens a context menu.
    REQUIRE(StylusButtonRouter::classifyPress(GDK_BUTTON_MIDDLE, GDK_SOURCE_MOUSE) ==
            Intent::Panning);
    REQUIRE(StylusButtonRouter::classifyPress(GDK_BUTTON_SECONDARY, GDK_SOURCE_MOUSE) ==
            Intent::ContextMenu);
    REQUIRE(StylusButtonRouter::classifyPress(GDK_BUTTON_PRIMARY, GDK_SOURCE_MOUSE) ==
            Intent::Inking);

    std::cout << "  [PASS] testBarrelButtonClaimsEraserWithoutStealingMouseClicks\n";
}

// The reported bug: a pen touch while the select tool was active started a text
// selection and consumed the press, so annotating required switching tools with a
// mouse first.
void testPenOverridesMouseOrientedTools() {
    // The mouse-only modes are exactly the ones a stylus must bypass.
    for (const char* tool : {"select", "text", "crop", "rect_select"}) {
        REQUIRE(StylusButtonRouter::resolveInkTool(tool, GDK_SOURCE_PEN, false) == "pen");
        REQUIRE(StylusButtonRouter::resolveInkTool(tool, GDK_SOURCE_ERASER, false) == "pen");
    }

    // Shift picks the highlighter nib.
    REQUIRE(StylusButtonRouter::resolveInkTool("select", GDK_SOURCE_PEN, true) == "highlighter");
    REQUIRE(StylusButtonRouter::resolveInkTool("text", GDK_SOURCE_ERASER, true) == "highlighter");

    // An explicitly chosen inking tool is honoured as selected. The eraser case is a
    // regression guard: an earlier version overrode unconditionally, so a stylus press
    // with the eraser selected drew a stroke at the eraser's forced width instead of
    // deleting anything.
    for (const char* tool : {"pen", "highlighter", "eraser"}) {
        REQUIRE(StylusButtonRouter::resolveInkTool(tool, GDK_SOURCE_PEN, false) == tool);
        REQUIRE(StylusButtonRouter::resolveInkTool(tool, GDK_SOURCE_ERASER, false) == tool);
    }

    // The highlighter modifier must not change an already-explicit tool choice.
    REQUIRE(StylusButtonRouter::resolveInkTool("pen", GDK_SOURCE_PEN, true) == "pen");
    REQUIRE(StylusButtonRouter::resolveInkTool("eraser", GDK_SOURCE_PEN, true) == "eraser");

    // A mouse keeps whatever tool the user selected.
    REQUIRE(StylusButtonRouter::resolveInkTool("select", GDK_SOURCE_MOUSE, false) == "select");
    REQUIRE(StylusButtonRouter::resolveInkTool("highlighter", GDK_SOURCE_MOUSE, false) ==
            "highlighter");
    REQUIRE(StylusButtonRouter::resolveInkTool("eraser", GDK_SOURCE_MOUSE, false) == "eraser");
    REQUIRE(StylusButtonRouter::resolveInkTool("select", GDK_SOURCE_TOUCHSCREEN, false) ==
            "select");

    std::cout << "  [PASS] testPenOverridesMouseOrientedTools\n";
}

void testBarrelRestoresPreviousTool() {
    StylusButtonRouter router;

    const std::string before = router.beginBarrel("highlighter");
    REQUIRE(before == "highlighter");
    REQUIRE(router.barrelActive());
    REQUIRE(!router.barrelLatched());

    // A short press restores whatever was selected.
    REQUIRE(router.endBarrel());
    REQUIRE(!router.barrelActive());

    // A latched eraser stays bound.
    StylusButtonRouter latched;
    latched.beginBarrel("pen");
    latched.latchBarrel();
    REQUIRE(latched.barrelLatched());
    REQUIRE(!latched.endBarrel());
    REQUIRE(latched.barrelLatched());

    // Repeated cycles do not leak stale state.
    StylusButtonRouter reused;
    for (int i = 0; i < 3; ++i) {
        reused.beginBarrel("pen");
        REQUIRE(reused.endBarrel());
    }
    REQUIRE(!reused.barrelActive());
    REQUIRE(!reused.barrelLatched());

    std::cout << "  [PASS] testBarrelRestoresPreviousTool\n";
}

void testLatchThresholdIsReasonable() {
    // Long enough to distinguish a deliberate hold from a click, short enough that a
    // user does not perceive it as lag.
    REQUIRE(StylusButtonRouter::kBarrelLatchMs >= 250);
    REQUIRE(StylusButtonRouter::kBarrelLatchMs <= 600);
    std::cout << "  [PASS] testLatchThresholdIsReasonable\n";
}

void testAttributeReadsAreSafeWithoutADevice() {
    StylusButtonRouter router;
    // Null event and null device must be tolerated rather than dereferenced.
    auto a = router.readAttributes(nullptr, nullptr);
    REQUIRE(!a.any());
    REQUIRE(!a.hasTilt);
    REQUIRE(!a.hasDistance);
    REQUIRE(!a.hasRotation);

    // A null event with a real device still must not read axes.
    StylusButtonRouter other;
    auto b = other.readAttributes(nullptr, nullptr);
    REQUIRE(!b.any());

    std::cout << "  [PASS] testAttributeReadsAreSafeWithoutADevice\n";
}

// Regression: the user changes tools while the pen is still down, then releases.
//
// The toolbar writes the new tool straight to the selection, bypassing whatever the
// stroke saved at press time. An unconditional restore on release therefore put the
// eraser back to the pen, so selecting the eraser appeared to do nothing and needed
// several presses of E before it took.
void testToolChangeDuringStrokeSurvivesRelease() {
    // Normal case: nothing else changed the tool, so the borrow unwinds.
    REQUIRE(StylusButtonRouter::shouldRestoreBorrowedTool("pen", "pen", "pen"));

    // The reported failure: pen down with "pen" installed, user presses E, so the
    // selection is now "eraser". The release must leave it alone.
    REQUIRE(!StylusButtonRouter::shouldRestoreBorrowedTool("pen", "pen", "eraser"));

    // Same story with the highlighter as the tool the user moved to.
    REQUIRE(!StylusButtonRouter::shouldRestoreBorrowedTool("pen", "pen", "highlighter"));

    // The eraser barrel borrow must not revert a tool chosen while it was held.
    REQUIRE(StylusButtonRouter::shouldRestoreBorrowedTool("highlighter", "eraser", "eraser"));
    REQUIRE(!StylusButtonRouter::shouldRestoreBorrowedTool("highlighter", "eraser", "pen"));

    // Nothing saved means nothing to restore.
    REQUIRE(!StylusButtonRouter::shouldRestoreBorrowedTool("", "pen", "pen"));

    std::cout << "  [PASS] testToolChangeDuringStrokeSurvivesRelease\n";
}

} // namespace

int main() {
    std::cout << "=== Running StylusButtonRouter Unit Tests ===\n";
    testDeviceSourceClassification();
    testBarrelButtonIsRecognizedOnPenOnly();
    testBarrelButtonClaimsEraserWithoutStealingMouseClicks();
    testPenOverridesMouseOrientedTools();
    testBarrelRestoresPreviousTool();
    testToolChangeDuringStrokeSurvivesRelease();
    testLatchThresholdIsReasonable();
    testAttributeReadsAreSafeWithoutADevice();
    std::cout << "=== All StylusButtonRouter Tests Passed! ===\n";
    return 0;
}
