#include "workspace/PhysicsSolver.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace FluidCore;

void testIntersectionAndOverlap() {
    Rectangle r1{0.0, 0.0, 100.0, 100.0};
    Rectangle r2{50.0, 0.0, 100.0, 100.0};

    Rectangle inter = PhysicsSolver::calculateIntersection(r1, r2);
    assert(std::abs(inter.x - 50.0) < 1e-6);
    assert(std::abs(inter.y - 0.0) < 1e-6);
    assert(std::abs(inter.w - 50.0) < 1e-6);
    assert(std::abs(inter.h - 100.0) < 1e-6);

    // Overlap area = 50 * 100 = 5000, min area = 10000 -> ratio = 0.5
    double ratio = PhysicsSolver::calculateOverlapRatio(r1, r2);
    assert(std::abs(ratio - 0.5) < 1e-6);

    // Disjoint
    Rectangle r3{200.0, 200.0, 50.0, 50.0};
    Rectangle interDisjoint = PhysicsSolver::calculateIntersection(r1, r3);
    assert(interDisjoint.w == 0.0 && interDisjoint.h == 0.0);
    assert(PhysicsSolver::calculateOverlapRatio(r1, r3) == 0.0);

    // Symmetric overlap test (small over large vs large over small)
    Rectangle rLarge{0.0, 0.0, 200.0, 200.0}; // area 40000
    Rectangle rSmall{50.0, 50.0, 50.0, 50.0}; // area 2500, fully inside rLarge
    assert(std::abs(PhysicsSolver::calculateOverlapRatio(rLarge, rSmall) - 1.0) < 1e-6);
    assert(std::abs(PhysicsSolver::calculateOverlapRatio(rSmall, rLarge) - 1.0) < 1e-6);

    std::cout << "[PASS] testIntersectionAndOverlap\n";
}

void testPrecedenceRuleStackMerge() {
    Rectangle targetBounds{100.0, 100.0, 200.0, 150.0};
    std::vector<CandidateTarget> candidates = {CandidateTarget{"card-1", targetBounds, false}};

    // Dragged card with > 50% overlap (e.g. 80% overlap)
    Rectangle dragOverlapping{120.0, 110.0, 200.0, 150.0};
    SnapResult res = PhysicsSolver::solveSnap(dragOverlapping, candidates, 16.0, 0.50);

    assert(res.type == SnapType::StackMerge);
    assert(res.targetNodeId == "card-1");
    assert(res.overlapRatio > 0.50);
    assert(res.guideLines.empty()); // Magnetic guidelines bypassed on stack merge

    // Dragged card with 40% overlap (<= 50%) -> should NOT trigger stack merge
    Rectangle dragLowOverlap{220.0, 100.0, 200.0, 150.0};
    SnapResult resLow = PhysicsSolver::solveSnap(dragLowOverlap, candidates, 16.0, 0.50);
    assert(resLow.type != SnapType::StackMerge);

    std::cout << "[PASS] testPrecedenceRuleStackMerge\n";
}

void testHorizontalMagneticSnapping() {
    Rectangle target{100.0, 100.0, 200.0, 150.0};
    std::vector<CandidateTarget> candidates = {{"card-target", target, false}};

    // 1. Left-to-Left snap (drag x=108 is within 16pt of target x=100)
    Rectangle dragLeft{108.0, 300.0, 200.0, 150.0};
    SnapResult resL = PhysicsSolver::solveSnap(dragLeft, candidates, 16.0, 0.50);
    assert(resL.type == SnapType::MagneticSnap);
    assert(std::abs(resL.snappedBounds.x - 100.0) < 1e-6);
    assert(!resL.guideLines.empty());

    // 2. Right-to-Right snap (target right = 300, drag right = 310 -> within 16pt)
    Rectangle dragRight{110.0, 300.0, 200.0, 150.0}; // right = 310
    SnapResult resR = PhysicsSolver::solveSnap(dragRight, candidates, 16.0, 0.50);
    assert(resR.type == SnapType::MagneticSnap);
    assert(std::abs((resR.snappedBounds.x + resR.snappedBounds.w) - 300.0) < 1e-6);

    // 3. Center-to-Center snap (target center = 200, drag center = 205)
    Rectangle dragCenter{130.0, 300.0, 150.0, 150.0}; // center = 130 + 75 = 205
    SnapResult resC = PhysicsSolver::solveSnap(dragCenter, candidates, 16.0, 0.50);
    assert(resC.type == SnapType::MagneticSnap);
    assert(std::abs((resC.snappedBounds.x + resC.snappedBounds.w * 0.5) - 200.0) < 1e-6);

    // 4. Dock Right (drag left = 308 near target right = 300)
    Rectangle dragDockR{308.0, 100.0, 100.0, 100.0};
    SnapResult resDR = PhysicsSolver::solveSnap(dragDockR, candidates, 16.0, 0.50);
    assert(resDR.type == SnapType::MagneticSnap);
    assert(std::abs(resDR.snappedBounds.x - 300.0) < 1e-6);

    // 5. Dock Left (drag right = 92 near target left = 100)
    Rectangle dragDockL{-8.0, 100.0, 100.0, 100.0}; // right = 92
    SnapResult resDL = PhysicsSolver::solveSnap(dragDockL, candidates, 16.0, 0.50);
    assert(resDL.type == SnapType::MagneticSnap);
    assert(std::abs((resDL.snappedBounds.x + resDL.snappedBounds.w) - 100.0) < 1e-6);

    std::cout << "[PASS] testHorizontalMagneticSnapping\n";
}

void testVerticalMagneticSnapping() {
    Rectangle target{100.0, 100.0, 200.0, 150.0};
    std::vector<CandidateTarget> candidates = {{"card-target", target, false}};

    // 1. Top-to-Top snap (drag y=106 near target y=100)
    Rectangle dragTop{400.0, 106.0, 200.0, 150.0};
    SnapResult resT = PhysicsSolver::solveSnap(dragTop, candidates, 16.0, 0.50);
    assert(resT.type == SnapType::MagneticSnap);
    assert(std::abs(resT.snappedBounds.y - 100.0) < 1e-6);

    // 2. Bottom-to-Bottom snap (target bottom = 250, drag bottom = 258)
    Rectangle dragBottom{400.0, 108.0, 200.0, 150.0}; // bottom = 258
    SnapResult resB = PhysicsSolver::solveSnap(dragBottom, candidates, 16.0, 0.50);
    assert(resB.type == SnapType::MagneticSnap);
    assert(std::abs((resB.snappedBounds.y + resB.snappedBounds.h) - 250.0) < 1e-6);

    // 3. Dock Below (drag top = 258 near target bottom = 250)
    Rectangle dragDockBelow{100.0, 258.0, 200.0, 150.0};
    SnapResult resDB = PhysicsSolver::solveSnap(dragDockBelow, candidates, 16.0, 0.50);
    assert(resDB.type == SnapType::MagneticSnap);
    assert(std::abs(resDB.snappedBounds.y - 250.0) < 1e-6);

    // 4. Dock Above (drag bottom = 95 near target top = 100)
    Rectangle dragDockAbove{100.0, -55.0, 200.0, 150.0}; // bottom = 95
    SnapResult resDA = PhysicsSolver::solveSnap(dragDockAbove, candidates, 16.0, 0.50);
    assert(resDA.type == SnapType::MagneticSnap);
    assert(std::abs((resDA.snappedBounds.y + resDA.snappedBounds.h) - 100.0) < 1e-6);

    std::cout << "[PASS] testVerticalMagneticSnapping\n";
}

void testIgnoreAndDistanceFiltering() {
    Rectangle target{100.0, 100.0, 200.0, 150.0};
    std::vector<CandidateTarget> candidates = {
        {"self-card", target, false}, {"far-card", Rectangle{1000.0, 1000.0, 200.0, 150.0}, false}};

    // Ignore self-card
    Rectangle dragBounds{105.0, 105.0, 200.0, 150.0};
    SnapResult res = PhysicsSolver::solveSnap(dragBounds, candidates, 16.0, 0.50, "self-card");
    assert(res.type == SnapType::None);

    // Beyond snap threshold (>16pt distance)
    Rectangle dragFar{500.0, 500.0, 100.0, 100.0};
    SnapResult resFar = PhysicsSolver::solveSnap(dragFar, candidates, 16.0, 0.50);
    assert(resFar.type == SnapType::None);

    std::cout << "[PASS] testIgnoreAndDistanceFiltering\n";
}

int main() {
    std::cout << "Running PhysicsSolverTest...\n";
    testIntersectionAndOverlap();
    testPrecedenceRuleStackMerge();
    testHorizontalMagneticSnapping();
    testVerticalMagneticSnapping();
    testIgnoreAndDistanceFiltering();
    std::cout << "All PhysicsSolverTest cases passed!\n";
    return 0;
}
