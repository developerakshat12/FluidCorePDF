#include "DamageRect.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << "\n";
        std::abort();
    }
}

} // namespace

using FluidCoreApp::DamageRect;

void testPointDamage() {
    // Point at (100.0, 100.0), strokeWidth = 2.0, padding = 4.0
    // radius = 1.0 + 4.0 = 5.0
    // box: x = 95, y = 95, w = 10, h = 10
    auto box = DamageRect::computePointDamage({100.0, 100.0}, 2.0, 4.0);
    expect(box.x == 95, "point damage x mismatch");
    expect(box.y == 95, "point damage y mismatch");
    expect(box.width == 10, "point damage width mismatch");
    expect(box.height == 10, "point damage height mismatch");

    std::cout << "[PASS] testPointDamage\n";
}

void testSegmentDamage() {
    // Horizontal segment from (10.0, 20.0) to (50.0, 20.0)
    // strokeWidth = 4.0, padding = 3.0 -> radius = 5.0
    // minX = 10 - 5 = 5, maxX = 50 + 5 = 55 -> w = 50
    // minY = 20 - 5 = 15, maxY = 20 + 5 = 25 -> h = 10
    auto box = DamageRect::computeSegmentDamage({10.0, 20.0}, {50.0, 20.0}, 4.0, 3.0);
    expect(box.x == 5, "horizontal segment x mismatch");
    expect(box.y == 15, "horizontal segment y mismatch");
    expect(box.width == 50, "horizontal segment width mismatch");
    expect(box.height == 10, "horizontal segment height mismatch");

    // Diagonal segment with floating point fractions
    auto diag = DamageRect::computeSegmentDamage({10.25, 20.75}, {30.5, 60.1}, 2.0, 4.0);
    // radius = 1 + 4 = 5.0
    // minX = 10.25 - 5.0 = 5.25 -> floor = 5
    // maxX = 30.5 + 5.0 = 35.5 -> ceil(35.5 - 5.25) = ceil(30.25) = 31
    expect(diag.x == 5, "diag x mismatch");
    expect(diag.y == 15, "diag y mismatch");
    expect(diag.width >= 30, "diag width should bound segment");
    expect(diag.height >= 44, "diag height should bound segment");

    std::cout << "[PASS] testSegmentDamage\n";
}

void testNegativeCoordinates() {
    auto box = DamageRect::computePointDamage({-10.5, -20.5}, 2.0, 4.0);
    // radius = 5.0
    // x = floor(-10.5 - 5.0) = floor(-15.5) = -16
    // y = floor(-20.5 - 5.0) = floor(-25.5) = -26
    expect(box.x == -16, "negative coord x floor mismatch");
    expect(box.y == -26, "negative coord y floor mismatch");
    expect(box.width == 10, "negative coord width mismatch");
    expect(box.height == 10, "negative coord height mismatch");

    std::cout << "[PASS] testNegativeCoordinates\n";
}

int main() {
    testPointDamage();
    testSegmentDamage();
    testNegativeCoordinates();
    std::cout << "All DamageRect tests passed successfully!\n";
    return 0;
}
