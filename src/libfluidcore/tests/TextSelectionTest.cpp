#include "text/TextSelection.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace FluidCore;

void testNormalization() {
    // Standard top-left to bottom-right
    auto r1 = TextSelection::normalize(10.0, 20.0, 50.0, 60.0);
    assert(r1.x0 == 10.0 && r1.y0 == 20.0 && r1.x1 == 50.0 && r1.y1 == 60.0);
    assert(!r1.isEmpty());
    assert(r1.width() == 40.0 && r1.height() == 40.0);

    // Inverted bottom-right to top-left
    auto r2 = TextSelection::normalize(50.0, 60.0, 10.0, 20.0);
    assert(r2.x0 == 10.0 && r2.y0 == 20.0 && r2.x1 == 50.0 && r2.y1 == 60.0);

    // Inverted X only
    auto r3 = TextSelection::normalize(50.0, 20.0, 10.0, 60.0);
    assert(r3.x0 == 10.0 && r3.y0 == 20.0 && r3.x1 == 50.0 && r3.y1 == 60.0);

    // Inverted Y only
    auto r4 = TextSelection::normalize(10.0, 60.0, 50.0, 20.0);
    assert(r4.x0 == 10.0 && r4.y0 == 20.0 && r4.x1 == 50.0 && r4.y1 == 60.0);

    // Zero area
    auto r5 = TextSelection::normalize(10.0, 20.0, 10.0, 20.0);
    assert(r5.isEmpty());
}

void testIntersectsAndUnite() {
    SelectionRect r1{10.0, 10.0, 50.0, 50.0};
    SelectionRect r2{30.0, 30.0, 70.0, 70.0};
    SelectionRect r3{60.0, 60.0, 90.0, 90.0};

    assert(TextSelection::intersects(r1, r2));
    assert(TextSelection::intersects(r2, r3));
    assert(!TextSelection::intersects(r1, r3));

    auto u12 = TextSelection::unite(r1, r2);
    assert(u12.x0 == 10.0 && u12.y0 == 10.0 && u12.x1 == 70.0 && u12.y1 == 70.0);

    SelectionRect emptyRect{0.0, 0.0, 0.0, 0.0};
    auto uEmpty = TextSelection::unite(r1, emptyRect);
    assert(uEmpty.x0 == r1.x0 && uEmpty.y0 == r1.y0 && uEmpty.x1 == r1.x1 && uEmpty.y1 == r1.y1);
}

void testCoalesceLineRects() {
    // Empty glyphs
    auto emptyRes = TextSelection::coalesceLineRects({});
    assert(emptyRes.empty());

    // Single line with closely spaced character boxes (simulating "FluidCore")
    std::vector<SelectionRect> line1Glyphs = {
        {10.0, 100.0, 18.0, 115.0}, // 'F'
        {19.0, 100.0, 23.0, 115.0}, // 'l'
        {24.0, 100.0, 30.0, 115.0}, // 'u'
        {31.0, 100.0, 35.0, 115.0}, // 'i'
        {36.0, 100.0, 44.0, 115.0}, // 'd'
        {45.0, 100.0, 53.0, 115.0}, // 'C'
        {54.0, 100.0, 62.0, 115.0}, // 'o'
        {63.0, 100.0, 69.0, 115.0}, // 'r'
        {70.0, 100.0, 77.0, 115.0}  // 'e'
    };

    auto coalesced1 = TextSelection::coalesceLineRects(line1Glyphs);
    assert(coalesced1.size() == 1);
    assert(coalesced1[0].x0 == 10.0);
    assert(coalesced1[0].x1 == 77.0);
    assert(coalesced1[0].y0 == 100.0);
    assert(coalesced1[0].y1 == 115.0);

    // Two lines of text
    std::vector<SelectionRect> multiLineGlyphs = line1Glyphs;
    // Line 2: "Platform" at y=130..145
    multiLineGlyphs.push_back({10.0, 130.0, 18.0, 145.0});
    multiLineGlyphs.push_back({19.0, 130.0, 24.0, 145.0});
    multiLineGlyphs.push_back({25.0, 130.0, 32.0, 145.0});
    multiLineGlyphs.push_back({33.0, 130.0, 39.0, 145.0});
    multiLineGlyphs.push_back({40.0, 130.0, 48.0, 145.0});
    multiLineGlyphs.push_back({49.0, 130.0, 57.0, 145.0});
    multiLineGlyphs.push_back({58.0, 130.0, 65.0, 145.0});
    multiLineGlyphs.push_back({66.0, 130.0, 78.0, 145.0});

    auto coalesced2 = TextSelection::coalesceLineRects(multiLineGlyphs);
    assert(coalesced2.size() == 2);
    assert(coalesced2[0].y0 == 100.0 && coalesced2[0].y1 == 115.0);
    assert(coalesced2[1].y0 == 130.0 && coalesced2[1].y1 == 145.0);
    assert(coalesced2[1].x0 == 10.0 && coalesced2[1].x1 == 78.0);
}

void testFormatClipboardText() {
    PageTextSelection p1;
    p1.pageIndex = 0;
    p1.text = "First paragraph on page 1.";

    PageTextSelection p2;
    p2.pageIndex = 1;
    p2.text = "Second paragraph on page 2.";

    std::vector<PageTextSelection> pages = {p1, p2};
    std::string formatted = TextSelection::formatClipboardText(pages);

    assert(formatted == "First paragraph on page 1.\n\nSecond paragraph on page 2.");

    // Empty page in between
    PageTextSelection pEmpty;
    pEmpty.pageIndex = 2;
    pages.push_back(pEmpty);

    PageTextSelection p3;
    p3.pageIndex = 3;
    p3.text = "Third paragraph.";
    pages.push_back(p3);

    std::string formatted2 = TextSelection::formatClipboardText(pages);
    assert(formatted2 == "First paragraph on page 1.\n\nSecond paragraph on page 2.\n\nThird "
                         "paragraph.");
}

void testDamageComputation() {
    SelectionRect r{10.2, 20.7, 50.4, 60.1};
    DamageBox damage = TextSelection::computeDamage(r, 100.0, 200.0, 4.0);

    // pageX = 100.0, pageY = 200.0
    // minX = 100.0 + 10.2 - 4.0 = 106.2 -> floor = 106
    // minY = 200.0 + 20.7 - 4.0 = 216.7 -> floor = 216
    // maxX = 100.0 + 50.4 + 4.0 = 154.4 -> ceil = 155
    // maxY = 200.0 + 60.1 + 4.0 = 264.1 -> ceil = 265
    assert(damage.x == 106);
    assert(damage.y == 216);
    assert(damage.width == (155 - 106));
    assert(damage.height == (265 - 216));

    DamageBox d2{150, 250, 40, 40};
    DamageBox united = TextSelection::uniteDamage(damage, d2);
    assert(united.x == 106);
    assert(united.y == 216);
    assert(united.x + united.width == 190);
    assert(united.y + united.height == 290);
}

void testMultiPageSelectionState() {
    MultiPageSelectionState state;
    assert(!state.hasSelection);
    assert(state.empty());

    state.hasSelection = true;
    state.startPage = 0;
    state.endPage = 1;
    PageTextSelection p0{0, {10, 10, 100, 20}, {{10, 10, 100, 20}}, "Hello"};
    state.pages.push_back(p0);
    state.fullText = "Hello";

    assert(state.hasSelection);
    assert(!state.empty());

    state.clear();
    assert(!state.hasSelection);
    assert(state.empty());
    assert(state.pages.empty());
    assert(state.fullText.empty());
}

int main() {
    std::cout << "Running TextSelectionTest suite..." << std::endl;
    testNormalization();
    testIntersectsAndUnite();
    testCoalesceLineRects();
    testFormatClipboardText();
    testDamageComputation();
    testMultiPageSelectionState();
    std::cout << "All TextSelectionTest cases passed successfully." << std::endl;
    return 0;
}
