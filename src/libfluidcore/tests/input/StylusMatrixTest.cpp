#include "input/PalmRejectionEngine.h"
#include "storage/ProjectStore.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace FluidCore;

static int failures = 0;

static bool check(bool condition, const std::string& desc) {
    if (!condition) {
        std::cerr << "FAIL: " << desc << "\n";
        failures++;
        return false;
    }
    std::cout << "PASS: " << desc << "\n";
    return true;
}

void testProfileDetection() {
    std::cout << "\n--- Testing Profile Detection (VID/PID & Name Fallback) ---\n";

    // 1. USB Vendor ID matching
    check(PalmRejectionEngine::detectProfile("056a", "037a", "Unknown Pen") ==
              StylusHardwareProfile::Wacom,
          "VID 056a detected as Wacom");
    check(PalmRejectionEngine::detectProfile("2D1F", "0001", "") == StylusHardwareProfile::Wacom,
          "VID 2d1f (case-insensitive) detected as Wacom");
    check(PalmRejectionEngine::detectProfile("045e", "096f", "") == StylusHardwareProfile::Surface,
          "VID 045e detected as Surface");
    check(PalmRejectionEngine::detectProfile("03f0", "1234", "") == StylusHardwareProfile::HpMpp,
          "VID 03f0 detected as HP MPP");

    // 2. Name matching fallback when VID is empty
    check(PalmRejectionEngine::detectProfile("", "", "Wacom Intuos Pro Pen") ==
              StylusHardwareProfile::Wacom,
          "Name 'Wacom Intuos Pro Pen' detected as Wacom");
    check(PalmRejectionEngine::detectProfile("", "", "Microsoft Surface Pen v4") ==
              StylusHardwareProfile::Surface,
          "Name 'Microsoft Surface Pen v4' detected as Surface");
    check(PalmRejectionEngine::detectProfile("", "", "HP Tilt Pen MPP 2.0") ==
              StylusHardwareProfile::HpMpp,
          "Name 'HP Tilt Pen MPP 2.0' detected as HP MPP");
    check(PalmRejectionEngine::detectProfile("", "", "ELAN Pen Digitizer") ==
              StylusHardwareProfile::HpMpp,
          "Name 'ELAN Pen Digitizer' detected as HP MPP");

    // 3. Name guard check: plain ELAN touchscreen without pen keyword must NOT be classified as pen
    check(PalmRejectionEngine::detectProfile("", "", "ELAN Touchscreen") ==
              StylusHardwareProfile::Generic,
          "Name 'ELAN Touchscreen' (no pen keyword) falls back to Generic");
    check(PalmRejectionEngine::detectProfile("", "", "Generic Tablet") ==
              StylusHardwareProfile::Generic,
          "Name 'Generic Tablet' falls back to Generic");
}

void testWacomMatrix() {
    std::cout << "\n--- Testing Wacom EMR/AES Profile Matrix ---\n";
    PalmRejectionEngine engine(PalmRejectionConfig::wacomDefaults());
    check(engine.currentProfile() == StylusHardwareProfile::Wacom,
          "Engine initialized with Wacom profile");

    // Hover suppression
    engine.onPenProximity(true, 100);
    check(engine.isPenInProximity(), "Pen is in hover proximity at T=100ms");
    check(engine.onTouchDown(1, 100.0, 100.0, 150) == InputDecision::RejectAsPalm,
          "Touch rejected while pen hovering at T=150ms");

    // Pen touchdown with pressure
    auto penDownRes = engine.onPenDown(150.0, 150.0, 0.75, 200, InputDeviceClass::Pen);
    check(penDownRes.accepted, "Pen-down accepted at T=200ms");
    check(!penDownRes.isEraser, "Pen-down identified as pen (not eraser)");
    check(engine.isPenInking(), "Pen inking state is active");
    check(engine.onTouchDown(2, 250.0, 250.0, 250) == InputDecision::RejectAsPalm,
          "Touch rejected while pen inking at T=250ms");

    // Pen motion
    auto penMoveRes = engine.onPenMotion(155.0, 155.0, 0.8, 260);
    check(penMoveRes.accepted, "Pen motion accepted at T=260ms");

    // Pen lift
    auto penUpRes = engine.onPenUp(160.0, 160.0, 300);
    check(penUpRes.accepted, "Pen-up accepted at T=300ms");
    check(!engine.isPenInking(), "Pen inking state is inactive after pen-up");

    // Hover exit
    engine.onPenProximity(false, 310);
    check(!engine.isPenInProximity(), "Pen exited hover proximity at T=310ms");

    // Post-hover cooldown (Wacom: 200ms hoverCooldown, 300ms strokeCooldown -> suppression holds
    // until T=510ms)
    check(engine.onTouchDown(3, 100.0, 100.0, 450) == InputDecision::RejectAsPalm,
          "Touch rejected during post-hover cooldown at T=450ms");
    check(engine.onTouchDown(4, 100.0, 100.0, 650) == InputDecision::Accept,
          "Touch accepted after cooldown expires at T=650ms");

    // Physical tail eraser test
    auto eraserRes = engine.onPenDown(100.0, 100.0, 0.5, 700, InputDeviceClass::Eraser);
    check(eraserRes.accepted, "Eraser-down accepted at T=700ms");
    check(eraserRes.isEraser, "DeviceClass::Eraser correctly identified as eraser");
    check(engine.isEraserActive(), "isEraserActive() is true");
    engine.onPenUp(100.0, 100.0, 750);
    engine.onPenProximity(false, 760);

    // Contact bounce deduplication test (Wacom: 20ms, 2.0px)
    auto press1 = engine.onPenDown(200.0, 200.0, 0.6, 1000, InputDeviceClass::Pen);
    check(press1.accepted && !press1.isDuplicateBounce, "Initial pen-down accepted at T=1000ms");
    auto bouncePress = engine.onPenDown(201.0, 200.5, 0.6, 1010, InputDeviceClass::Pen);
    check(!bouncePress.accepted && bouncePress.isDuplicateBounce,
          "Rapid duplicate pen-down (dt=10ms, dist=1.12px) rejected as bounce chatter");
    engine.onPenUp(201.0, 200.5, 1100);
}

void testHpMppMatrix() {
    std::cout << "\n--- Testing HP MPP Profile Matrix (Pre-Contact Palm Quarantine) ---\n";
    PalmRejectionEngine engine(PalmRejectionConfig::hpMppDefaults());
    check(engine.currentProfile() == StylusHardwareProfile::HpMpp,
          "Engine initialized with HP MPP profile");

    // Scenario: Palm lands on capacitive glass at T=100ms before pen tip touches
    auto touchDecision = engine.onTouchDown(10, 200.0, 300.0, 100);
    check(touchDecision == InputDecision::Accept,
          "Palm pre-contact touch initially accepted at T=100ms");

    // Pen tip touches glass at T=160ms (dt=60ms <= 100ms retroactive cancel window) near palm
    auto penDown = engine.onPenDown(220.0, 280.0, 0.7, 160, InputDeviceClass::Pen);
    check(penDown.accepted, "Pen-down accepted at T=160ms");
    check(penDown.cancelledTouchIds.size() == 1 && penDown.cancelledTouchIds[0] == 10,
          "Retroactive quarantine cancelled prior touch #10");
    check(engine.isTouchCancelled(10), "Touch #10 marked as cancelled in engine");

    // Subsequent motion of cancelled touch is rejected immediately
    check(engine.onTouchMotion(10, 205.0, 305.0, 170) == InputDecision::RejectAsPalm,
          "Subsequent motion of cancelled touch rejected as palm at T=170ms");
    check(engine.onTouchUp(10, 205.0, 305.0, 180) == InputDecision::RejectAsPalm,
          "Release of cancelled touch rejected as palm at T=180ms");

    // Pen drawing at 250Hz rate
    for (int t = 170; t <= 300; t += 4) {
        engine.onPenMotion(220.0 + (t - 160) * 0.5, 280.0, 0.7, t);
    }
    engine.onPenUp(290.0, 280.0, 305);
    engine.onPenProximity(false, 310);

    // Cooldown test (HP MPP: 400ms strokeCooldown, holds until T=705ms)
    check(engine.onTouchDown(11, 500.0, 500.0, 500) == InputDecision::RejectAsPalm,
          "Touch rejected during HP MPP cooldown at T=500ms");
    check(engine.onTouchDown(12, 500.0, 500.0, 750) == InputDecision::Accept,
          "Touch accepted after HP MPP cooldown at T=750ms");

    // Contact bounce deduplication test (HP MPP: 35ms, 3.5px)
    auto p1 = engine.onPenDown(300.0, 300.0, 0.5, 1000, InputDeviceClass::Pen);
    check(p1.accepted && !p1.isDuplicateBounce, "Initial pen-down accepted at T=1000ms");
    auto pBounce = engine.onPenDown(302.0, 301.0, 0.5, 1025, InputDeviceClass::Pen);
    check(!pBounce.accepted && pBounce.isDuplicateBounce,
          "Duplicate pen-down (dt=25ms <= 35ms, dist=2.23px) rejected as bounce chatter on HP MPP");
    engine.onPenUp(302.0, 301.0, 1100);
}

void testSurfaceMatrix() {
    std::cout
        << "\n--- Testing Microsoft Surface Matrix (Multi-Touch Cluster & Dynamic Radius) ---\n";
    PalmRejectionEngine engine(PalmRejectionConfig::surfaceDefaults());
    check(engine.currentProfile() == StylusHardwareProfile::Surface,
          "Engine initialized with Surface profile");

    // Pen inking at (100, 100)
    engine.onPenDown(100.0, 100.0, 0.8, 100, InputDeviceClass::Pen);

    // Multi-touch palm cluster landing near pen tip
    check(engine.onTouchDown(20, 130.0, 120.0, 110) == InputDecision::RejectAsPalm,
          "Palm cluster touch #20 (dist=36px) rejected by spatial radius & inking state");
    check(engine.onTouchDown(21, 140.0, 130.0, 112) == InputDecision::RejectAsPalm,
          "Palm cluster touch #21 (dist=50px) rejected by spatial radius & inking state");
    check(engine.onTouchDown(22, 120.0, 150.0, 115) == InputDecision::RejectAsPalm,
          "Palm cluster touch #22 (dist=53px) rejected by spatial radius & inking state");

    // Dynamic spatial radius tracking:
    // A distant touch at (800, 800) lands before pen approaches (if pen lifted and idle)
    engine.onPenUp(100.0, 100.0, 120);
    engine.onPenProximity(false, 130);
    // Cooldown expires at T=600ms
    check(engine.onTouchDown(30, 500.0, 500.0, 700) == InputDecision::Accept,
          "Distant touch accepted while pen idle at T=700ms");

    // Pen begins drawing at (450, 450) and moves toward (500, 500)
    engine.onPenDown(450.0, 450.0, 0.7, 720, InputDeviceClass::Pen);
    // When touch #30 moves, dynamic evaluation against active pen at (450, 450) evaluates dist <=
    // 220px
    check(engine.onTouchMotion(30, 500.0, 500.0, 725) == InputDecision::RejectAsPalm,
          "Touch #30 dynamically rejected as active pen draws near it (dist=70.7px <= 220px)");
    check(engine.isTouchCancelled(30), "Touch #30 dynamically marked cancelled");

    engine.onPenUp(450.0, 450.0, 750);
    engine.onPenProximity(false, 760);

    // Contact bounce deduplication test (Surface: 30ms, 3.0px)
    auto s1 = engine.onPenDown(400.0, 400.0, 0.7, 1000, InputDeviceClass::Pen);
    check(s1.accepted && !s1.isDuplicateBounce, "Initial pen-down accepted at T=1000ms");
    auto sBounce = engine.onPenDown(401.5, 401.0, 0.7, 1020, InputDeviceClass::Pen);
    check(
        !sBounce.accepted && sBounce.isDuplicateBounce,
        "Duplicate pen-down (dt=20ms <= 30ms, dist=1.80px) rejected as bounce chatter on Surface");
    engine.onPenUp(401.5, 401.0, 1100);
}

void testInputArbitration() {
    std::cout << "\n--- Testing Multi-Channel Input Arbitration ---\n";
    PalmRejectionEngine engine(PalmRejectionConfig::genericDefaults());

    // Simultaneous pen inking + mouse input
    engine.onPenDown(100.0, 100.0, 0.7, 100, InputDeviceClass::Pen);
    check(engine.onMouseDown(500.0, 500.0, 110) == InputDecision::Accept,
          "Mouse events (e.g. scroll wheel) accepted during active pen inking");

    // Touch remains rejected
    check(engine.onTouchDown(1, 120.0, 120.0, 120) == InputDecision::RejectAsPalm,
          "Touch strictly rejected while pen is inking");

    engine.onPenUp(100.0, 100.0, 150);
    engine.onPenProximity(false, 160);
}

void testPressurePersistenceRoundTrip() {
    std::cout << "\n--- Testing Pressure Persistence Round-Trip & Strict Cross-Validation ---\n";

    std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "fluidcore_stylus_test";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    std::string projPath = (tempDir / "stylus_test.ltproj").string();
    std::string err;

    ProjectStore store;
    check(store.openProject(projPath, &err), "Opened new test project for pressure round-trip");

    // 1. Create a stroke with per-point pressures
    Stroke strokeWithPressure;
    strokeWithPressure.id = "stroke_pressure_1";
    strokeWithPressure.tool = "pen";
    strokeWithPressure.color = 0x00FF00;
    strokeWithPressure.width = 4.0;
    strokeWithPressure.timestamp = 1000;
    strokeWithPressure.points = {{10.0, 20.0}, {15.0, 25.0}, {20.0, 30.0}, {25.0, 35.0}};
    strokeWithPressure.pressures = {0.25, 0.50, 0.75, 1.00};

    // 2. Create a stroke with NO pressures (mouse drawn / pre-migration style)
    Stroke strokeNoPressure;
    strokeNoPressure.id = "stroke_no_pressure_2";
    strokeNoPressure.tool = "highlighter";
    strokeNoPressure.color = 0xFFFF00;
    strokeNoPressure.width = 12.0;
    strokeNoPressure.timestamp = 2000;
    strokeNoPressure.points = {{100.0, 100.0}, {110.0, 110.0}};
    // pressures is empty

    WorkspaceModel saveModel("stylus_test");
    saveModel.insert(std::make_unique<CanvasStrokeNode>(strokeWithPressure));
    saveModel.insert(std::make_unique<CanvasStrokeNode>(strokeNoPressure));

    GraphTopology graph;
    std::vector<DocumentRecord> docs;
    check(store.saveProject(saveModel, graph, docs, &err),
          "Saved model with pressure strokes to project.db");
    store.closeProject();

    // Rehydrate and verify
    ProjectStore loadStore;
    check(loadStore.openProject(projPath, &err), "Reopened project to rehydrate strokes");
    WorkspaceModel loadModel("stylus_test");
    GraphTopology loadGraph;
    std::vector<DocumentRecord> loadDocs;
    check(loadStore.rehydrate(loadModel, loadGraph, loadDocs, &err), "Rehydrated project");

    const auto* loadedStrokeNode1 =
        dynamic_cast<const CanvasStrokeNode*>(loadModel.find("stroke_pressure_1"));
    check(loadedStrokeNode1 != nullptr, "Found loaded stroke_pressure_1");
    if (loadedStrokeNode1) {
        const auto& s = loadedStrokeNode1->stroke();
        check(s.points.size() == 4, "Point count is 4");
        check(s.pressures.size() == 4, "Pressure count is 4");
        bool pressMatch = true;
        for (size_t i = 0; i < s.pressures.size(); ++i) {
            if (std::abs(s.pressures[i] - strokeWithPressure.pressures[i]) > 1e-5) {
                pressMatch = false;
            }
        }
        check(pressMatch, "Per-point pressures losslessly rehydrated: [0.25, 0.50, 0.75, 1.00]");
    }

    const auto* loadedStrokeNode2 =
        dynamic_cast<const CanvasStrokeNode*>(loadModel.find("stroke_no_pressure_2"));
    check(loadedStrokeNode2 != nullptr, "Found loaded stroke_no_pressure_2");
    if (loadedStrokeNode2) {
        const auto& s = loadedStrokeNode2->stroke();
        check(s.points.size() == 2, "Point count is 2");
        check(s.pressures.empty(),
              "pressures is empty when pressures_blob was NULL (fallback to base_width)");
        check(s.width == 12.0, "base_width 12.0 preserved");
    }

    loadStore.closeProject();
    std::filesystem::remove_all(tempDir);
}

int main() {
    std::cout << "===================================================\n";
    std::cout << "   FluidCore Stylus Matrix & Palm Rejection Suite   \n";
    std::cout << "===================================================\n";

    testProfileDetection();
    testWacomMatrix();
    testHpMppMatrix();
    testSurfaceMatrix();
    testInputArbitration();
    testPressurePersistenceRoundTrip();

    std::cout << "\n===================================================\n";
    if (failures == 0) {
        std::cout << "   ALL MATRIX & PALM REJECTION TESTS PASSED!       \n";
        std::cout << "===================================================\n";
        return 0;
    } else {
        std::cerr << "   " << failures << " TESTS FAILED!\n";
        std::cout << "===================================================\n";
        return 1;
    }
}
