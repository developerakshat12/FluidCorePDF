#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace FluidCore {

struct SelectionPoint {
    double x = 0.0;
    double y = 0.0;
};

struct SelectionRect {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;

    bool isEmpty() const { return (x1 <= x0) || (y1 <= y0); }
    double width() const { return std::max(0.0, x1 - x0); }
    double height() const { return std::max(0.0, y1 - y0); }
};

struct PageTextSelection {
    std::size_t pageIndex = 0;
    SelectionRect dragBounds;
    std::vector<SelectionRect> lineRects;
    std::string text;

    bool empty() const { return lineRects.empty() && text.empty(); }
};

struct MultiPageSelectionState {
    bool hasSelection = false;
    std::size_t startPage = 0;
    std::size_t endPage = 0;
    SelectionPoint startPoint;
    SelectionPoint endPoint;
    std::vector<PageTextSelection> pages;
    std::string fullText;

    void clear() {
        hasSelection = false;
        startPage = 0;
        endPage = 0;
        startPoint = {0.0, 0.0};
        endPoint = {0.0, 0.0};
        pages.clear();
        fullText.clear();
    }

    bool empty() const { return !hasSelection || pages.empty(); }
};

struct DamageBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isEmpty() const { return width <= 0 || height <= 0; }
};

class TextSelection {
  public:
    static SelectionRect normalize(double startX, double startY, double endX, double endY);

    static bool intersects(const SelectionRect& a, const SelectionRect& b);

    static SelectionRect unite(const SelectionRect& a, const SelectionRect& b);

    // Coalesce discrete glyph or word bounding boxes into contiguous horizontal line highlight
    // strips.
    static std::vector<SelectionRect>
    coalesceLineRects(const std::vector<SelectionRect>& glyphRects, double lineToleranceY = 3.0);

    // Format and concatenate multi-page extracted text for clipboard buffer.
    static std::string formatClipboardText(const std::vector<PageTextSelection>& pages);

    // Compute pixel damage bounding box for a single selection rectangle on a page.
    static DamageBox computeDamage(const SelectionRect& rect, double pageX, double pageY,
                                   double padding = 4.0);

    // Compute aggregate pixel damage bounding box across all lines of a page selection.
    static DamageBox computePageDamage(const PageTextSelection& pageSel, double pageX, double pageY,
                                       double padding = 4.0);

    // Compute united pixel damage bounding box between previous and current selection states.
    static DamageBox uniteDamage(const DamageBox& a, const DamageBox& b);
};

} // namespace FluidCore
