#include "services/ToolManager.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << "\n";
        std::abort();
    }
}

} // namespace

using FluidCoreApp::Tool;
using FluidCoreApp::ToolManager;

void testDefaultState() {
    ToolManager tm;
    expect(tm.activeTool() == Tool::Select, "Initial active tool should be Select");
    expect(tm.lastInkingTool() == Tool::Pen, "Initial last inking tool should default to Pen");
    std::cout << "[PASS] testDefaultState\n";
}

void testPenTracking() {
    ToolManager tm;
    tm.setActiveTool(Tool::Pen);
    expect(tm.activeTool() == Tool::Pen, "Active tool should be Pen");
    expect(tm.lastInkingTool() == Tool::Pen, "Last inking tool should be Pen");

    // Switching to non-inking tool shouldn't overwrite last inking tool
    tm.setActiveTool(Tool::Select);
    expect(tm.activeTool() == Tool::Select, "Active tool should be Select");
    expect(tm.lastInkingTool() == Tool::Pen, "Last inking tool should remain Pen");
    std::cout << "[PASS] testPenTracking\n";
}

void testHighlighterTracking() {
    ToolManager tm;
    tm.setActiveTool(Tool::Highlighter);
    expect(tm.activeTool() == Tool::Highlighter, "Active tool should be Highlighter");
    expect(tm.lastInkingTool() == Tool::Highlighter, "Last inking tool should be Highlighter");

    // Switching to Crop shouldn't overwrite last inking tool
    tm.setActiveTool(Tool::Crop);
    expect(tm.activeTool() == Tool::Crop, "Active tool should be Crop");
    expect(tm.lastInkingTool() == Tool::Highlighter, "Last inking tool should remain Highlighter");
    std::cout << "[PASS] testHighlighterTracking\n";
}

void testToggleEraserCycle() {
    ToolManager tm;

    // 1. Pen -> Eraser -> Pen
    tm.setActiveTool(Tool::Pen);
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Eraser, "toggleEraser from Pen should switch to Eraser");
    expect(tm.lastInkingTool() == Tool::Pen, "last inking tool should still be Pen");

    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Pen, "toggleEraser from Eraser should return to Pen");

    // 2. Highlighter -> Eraser -> Highlighter
    tm.setActiveTool(Tool::Highlighter);
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Eraser,
           "toggleEraser from Highlighter should switch to Eraser");
    expect(tm.lastInkingTool() == Tool::Highlighter, "last inking tool should be Highlighter");

    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Highlighter,
           "toggleEraser from Eraser should return to Highlighter");

    // 3. Repeat Pen cycle
    tm.setActiveTool(Tool::Pen);
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Eraser, "Second Pen toggle to Eraser failed");
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Pen, "Second Pen toggle return failed");

    std::cout << "[PASS] testToggleEraserCycle\n";
}

void testExplicitToolOverrideDuringEraser() {
    ToolManager tm;
    tm.setActiveTool(Tool::Highlighter);
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Eraser, "Should be Eraser");

    // Pressing 'P' explicitly while on Eraser
    tm.setActiveTool(Tool::Pen);
    expect(tm.activeTool() == Tool::Pen, "Explicit Pen should switch to Pen");
    expect(tm.lastInkingTool() == Tool::Pen, "Last inking tool should be updated to Pen");

    std::cout << "[PASS] testExplicitToolOverrideDuringEraser\n";
}

void testToggleFromNonInkingTool() {
    ToolManager tm;
    expect(tm.activeTool() == Tool::Select, "Should start at Select");
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Eraser, "toggleEraser from Select should switch to Eraser");
    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Pen, "toggleEraser from Eraser should return to default Pen");
    std::cout << "[PASS] testToggleFromNonInkingTool\n";
}

void testToolFromStringAliases() {
    expect(ToolManager::toolFromString("pen") == Tool::Pen, "pen alias");
    expect(ToolManager::toolFromString("brush") == Tool::Pen, "brush alias for tablet drivers");
    expect(ToolManager::toolFromString("BRUSH") == Tool::Pen, "case-insensitive brush alias");
    expect(ToolManager::toolFromString("eraser") == Tool::Eraser, "eraser alias");
    expect(ToolManager::toolFromString("erase") == Tool::Eraser, "erase alias");
    expect(ToolManager::toolFromString("highlighter") == Tool::Highlighter, "highlighter alias");
    expect(ToolManager::toolFromString("highlight") == Tool::Highlighter, "highlight alias");
    std::cout << "[PASS] testToolFromStringAliases\n";
}

void testChangeListenerDispatch() {
    ToolManager tm;
    std::vector<Tool> dispatched;
    tm.addChangeListener([&dispatched](Tool tool) { dispatched.push_back(tool); });

    tm.setActiveTool(Tool::Pen);
    tm.toggleEraser();
    tm.toggleEraser();

    expect(dispatched.size() == 3, "Should have 3 notifications");
    expect(dispatched[0] == Tool::Pen, "First notification Pen");
    expect(dispatched[1] == Tool::Eraser, "Second notification Eraser");
    expect(dispatched[2] == Tool::Pen, "Third notification Pen");
    std::cout << "[PASS] testChangeListenerDispatch\n";
}

void testInkPropertiesDefaults() {
    ToolManager tm;
    auto penProps = tm.propertiesForTool(Tool::Pen);
    expect(penProps.color == 0x0F172A, "Default Pen color should be 0x0F172A");
    expect(penProps.width == 2.0, "Default Pen width should be 2.0");

    auto hlProps = tm.propertiesForTool(Tool::Highlighter);
    expect(hlProps.color == 0xFACC15, "Default Highlighter color should be 0xFACC15");
    expect(hlProps.width == 14.0, "Default Highlighter width should be 14.0");

    std::cout << "[PASS] testInkPropertiesDefaults\n";
}

void testPerToolMemory() {
    ToolManager tm;

    // 1. Customize Pen
    tm.setActiveTool(Tool::Pen);
    tm.setActiveColor(0x2563EB);
    tm.setActiveWidth(4.5);
    expect(tm.activeColor() == 0x2563EB, "Pen active color customized");
    expect(tm.activeWidth() == 4.5, "Pen active width customized");

    // 2. Customize Highlighter
    tm.setActiveTool(Tool::Highlighter);
    expect(tm.activeColor() == 0xFACC15, "Highlighter starts at its own default color");
    expect(tm.activeWidth() == 14.0, "Highlighter starts at its own default width");

    tm.setActiveColor(0xEC4899);
    tm.setActiveWidth(22.0);
    expect(tm.activeColor() == 0xEC4899, "Highlighter active color customized");
    expect(tm.activeWidth() == 22.0, "Highlighter active width customized");

    // 3. Switch back to Pen and verify memory preservation
    tm.setActiveTool(Tool::Pen);
    expect(tm.activeColor() == 0x2563EB, "Pen remembered its custom color");
    expect(tm.activeWidth() == 4.5, "Pen remembered its custom width");

    // 4. Switch back to Highlighter and verify memory preservation
    tm.setActiveTool(Tool::Highlighter);
    expect(tm.activeColor() == 0xEC4899, "Highlighter remembered its custom color");
    expect(tm.activeWidth() == 22.0, "Highlighter remembered its custom width");

    std::cout << "[PASS] testPerToolMemory\n";
}

void testPropertyChangeNotifications() {
    ToolManager tm;
    std::vector<std::pair<Tool, FluidCoreApp::InkProperties>> propEvents;
    tm.addPropertyChangeListener([&propEvents](Tool t, const FluidCoreApp::InkProperties& p) {
        propEvents.push_back({t, p});
    });

    tm.setActiveTool(Tool::Pen);
    // Setting active tool triggers a notification for that tool
    expect(!propEvents.empty(), "setActiveTool should trigger property notification");
    expect(propEvents.back().first == Tool::Pen, "Event tool should be Pen");

    tm.setActiveColor(0xEF4444);
    expect(propEvents.back().second.color == 0xEF4444, "Color change notified");

    tm.setActiveWidth(6.0);
    expect(propEvents.back().second.width == 6.0, "Width change notified");

    std::cout << "[PASS] testPropertyChangeNotifications\n";
}

void testWidthClampingAndStepping() {
    ToolManager tm;
    tm.setActiveTool(Tool::Pen);
    tm.setActiveWidth(3.7);

    // Step up
    tm.stepActiveWidth(FluidCoreApp::InkingLimits::PenStepWidth);
    expect(std::abs(tm.activeWidth() - 4.2) < 1e-6, "Step up preserves continuous values");

    // Step down
    tm.stepActiveWidth(-FluidCoreApp::InkingLimits::PenStepWidth);
    expect(std::abs(tm.activeWidth() - 3.7) < 1e-6, "Step down returns to continuous value");

    // Clamp at minimum
    tm.stepActiveWidth(-100.0);
    expect(tm.activeWidth() == FluidCoreApp::InkingLimits::PenMinWidth, "Clamps to PenMinWidth");

    // Clamp at maximum
    tm.stepActiveWidth(200.0);
    expect(tm.activeWidth() == FluidCoreApp::InkingLimits::PenMaxWidth, "Clamps to PenMaxWidth");

    // Test Highlighter limits
    tm.setActiveTool(Tool::Highlighter);
    tm.stepActiveWidth(-100.0);
    expect(tm.activeWidth() == FluidCoreApp::InkingLimits::HighlighterMinWidth,
           "Clamps to HighlighterMinWidth");

    tm.stepActiveWidth(200.0);
    expect(tm.activeWidth() == FluidCoreApp::InkingLimits::HighlighterMaxWidth,
           "Clamps to HighlighterMaxWidth");

    std::cout << "[PASS] testWidthClampingAndStepping\n";
}

void testEraserTogglePreservesProperties() {
    ToolManager tm;
    tm.setActiveTool(Tool::Pen);
    tm.setActiveColor(0x2563EB);
    tm.setActiveWidth(4.5);

    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Eraser, "Active tool is now Eraser");
    expect(tm.activeProperties().color == 0x2563EB,
           "activeProperties on Eraser reflects last inking tool");
    expect(tm.activeProperties().width == 4.5, "activeWidth on Eraser reflects last inking tool");

    tm.toggleEraser();
    expect(tm.activeTool() == Tool::Pen, "Returned to Pen");
    expect(tm.activeColor() == 0x2563EB, "Pen color preserved after Eraser toggle");
    expect(tm.activeWidth() == 4.5, "Pen width preserved after Eraser toggle");

    std::cout << "[PASS] testEraserTogglePreservesProperties\n";
}

int main() {
    std::cout << "=== Running ToolManager Tests ===\n";
    testDefaultState();
    testPenTracking();
    testHighlighterTracking();
    testToggleEraserCycle();
    testExplicitToolOverrideDuringEraser();
    testToggleFromNonInkingTool();
    testToolFromStringAliases();
    testChangeListenerDispatch();
    testInkPropertiesDefaults();
    testPerToolMemory();
    testPropertyChangeNotifications();
    testWidthClampingAndStepping();
    testEraserTogglePreservesProperties();
    std::cout << "=== All ToolManager Tests Passed! ===\n";
    return 0;
}
