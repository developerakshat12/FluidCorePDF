#include "ThumbnailLayout.h"

#include <cmath>
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

using FluidCoreApp::ThumbnailLayout;

void testEmptyPages() {
    std::vector<ThumbnailLayout::PageDimension> empty;
    auto result = ThumbnailLayout::computeLayout(empty);
    expect(result.boxes.empty(), "boxes should be empty");
    expect(result.totalHeight == 0.0, "totalHeight should be 0");

    auto hit = ThumbnailLayout::findPageAtY(result.boxes, 50.0);
    expect(!hit.has_value(), "hit should be nullopt for empty pages");

    auto active = ThumbnailLayout::findActivePage(empty, 0.0, 500.0);
    expect(active == 0, "active should be 0 for empty pages");
    std::cout << "[PASS] testEmptyPages\n";
}

void testUniformLayoutAndScaling() {
    ThumbnailLayout::LayoutConfig config;
    config.targetWidth = 140.0;
    config.margin = 10.0;
    config.gap = 10.0;
    config.labelHeight = 20.0;

    // 3 standard portrait pages (width 600, height 800)
    // availWidth = 140 - 20 = 120
    // scale = 120 / 600 = 0.2
    // thumbW = 120, thumbH = 160
    std::vector<ThumbnailLayout::PageDimension> pages = {
        {600.0, 800.0, 0.0}, {600.0, 800.0, 810.0}, {600.0, 800.0, 1620.0}};

    auto result = ThumbnailLayout::computeLayout(pages, config);
    expect(result.boxes.size() == 3, "expected 3 boxes");

    // Box 0: y = margin = 10.0, h = 160
    expect(std::abs(result.boxes[0].y - 10.0) < 1e-6, "box 0 y mismatch");
    expect(std::abs(result.boxes[0].width - 120.0) < 1e-6, "box 0 width mismatch");
    expect(std::abs(result.boxes[0].height - 160.0) < 1e-6, "box 0 height mismatch");
    expect(result.boxes[0].pageIndex == 0, "box 0 index mismatch");

    // Next Y: 10 + 160 (thumb) + 20 (label) + 10 (gap) = 200.0
    expect(std::abs(result.boxes[1].y - 200.0) < 1e-6, "box 1 y mismatch");
    expect(result.boxes[1].pageIndex == 1, "box 1 index mismatch");

    // Next Y: 200 + 160 + 20 + 10 = 390.0
    expect(std::abs(result.boxes[2].y - 390.0) < 1e-6, "box 2 y mismatch");
    expect(result.boxes[2].pageIndex == 2, "box 2 index mismatch");

    std::cout << "[PASS] testUniformLayoutAndScaling\n";
}

void testHitTesting() {
    ThumbnailLayout::LayoutConfig config;
    config.targetWidth = 140.0;
    config.margin = 10.0;
    config.gap = 10.0;
    config.labelHeight = 20.0;

    std::vector<ThumbnailLayout::PageDimension> pages = {
        {600.0, 800.0, 0.0}, {600.0, 800.0, 810.0}, {600.0, 800.0, 1620.0}};

    auto result = ThumbnailLayout::computeLayout(pages, config);

    // Click at y = 5.0 (above box 0) -> should clamp to page 0
    auto hitTop = ThumbnailLayout::findPageAtY(result.boxes, 5.0, config);
    expect(hitTop.has_value() && *hitTop == 0, "hitTop should be page 0");

    // Click at y = 80.0 (inside box 0) -> page 0
    auto hit0 = ThumbnailLayout::findPageAtY(result.boxes, 80.0, config);
    expect(hit0.has_value() && *hit0 == 0, "hit0 should be page 0");

    // Click at y = 250.0 (inside box 1) -> page 1
    auto hit1 = ThumbnailLayout::findPageAtY(result.boxes, 250.0, config);
    expect(hit1.has_value() && *hit1 == 1, "hit1 should be page 1");

    // Click at y = 450.0 (inside box 2) -> page 2
    auto hit2 = ThumbnailLayout::findPageAtY(result.boxes, 450.0, config);
    expect(hit2.has_value() && *hit2 == 2, "hit2 should be page 2");

    // Click below box 2 -> clamps to page 2
    auto hitBottom = ThumbnailLayout::findPageAtY(result.boxes, 900.0, config);
    expect(hitBottom.has_value() && *hitBottom == 2, "hitBottom should be page 2");

    std::cout << "[PASS] testHitTesting\n";
}

void testActivePageResolution() {
    // 3 pages with height 1000 and gap 20:
    // Page 0: docY = 0, center = 500
    // Page 1: docY = 1020, center = 1520
    // Page 2: docY = 2040, center = 2540
    std::vector<ThumbnailLayout::PageDimension> pages = {
        {600.0, 1000.0, 0.0}, {600.0, 1000.0, 1020.0}, {600.0, 1000.0, 2040.0}};

    const double viewportHeight = 600.0;

    // Viewport at top (viewportY = 0 -> center = 300) -> Page 0 (center 500) is closest
    expect(ThumbnailLayout::findActivePage(pages, 0.0, viewportHeight) == 0,
           "active page should be 0");

    // Scroll down so viewport center is 1000:
    // dist to page 0 center (500) = 500
    // dist to page 1 center (1520) = 520
    // Page 0 is still closer
    expect(ThumbnailLayout::findActivePage(pages, 700.0, viewportHeight) == 0,
           "active page should be 0 at y=700");

    // Scroll slightly more so viewport center is 1020 (viewportY = 720.0):
    // dist to page 0 center (500) = 520
    // dist to page 1 center (1520) = 500
    // Page 1 is now closer
    expect(ThumbnailLayout::findActivePage(pages, 720.0, viewportHeight) == 1,
           "active page should be 1 at y=720");

    // Viewport near bottom (viewportY = 2200 -> center = 2500) -> Page 2 is closest
    expect(ThumbnailLayout::findActivePage(pages, 2200.0, viewportHeight) == 2,
           "active page should be 2 at y=2200");

    std::cout << "[PASS] testActivePageResolution\n";
}

int main() {
    testEmptyPages();
    testUniformLayoutAndScaling();
    testHitTesting();
    testActivePageResolution();
    std::cout << "All ThumbnailLayout tests passed successfully!\n";
    return 0;
}
