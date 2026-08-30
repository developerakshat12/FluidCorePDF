#include "text/TextSelection.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace FluidCore {

SelectionRect TextSelection::normalize(double startX, double startY, double endX, double endY) {
    return SelectionRect{std::min(startX, endX), std::min(startY, endY), std::max(startX, endX),
                         std::max(startY, endY)};
}

bool TextSelection::intersects(const SelectionRect& a, const SelectionRect& b) {
    return !(a.x1 < b.x0 || a.x0 > b.x1 || a.y1 < b.y0 || a.y0 > b.y1);
}

SelectionRect TextSelection::unite(const SelectionRect& a, const SelectionRect& b) {
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    return SelectionRect{std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1),
                         std::max(a.y1, b.y1)};
}

std::vector<SelectionRect>
TextSelection::coalesceLineRects(const std::vector<SelectionRect>& glyphRects,
                                 double lineToleranceY) {
    std::vector<SelectionRect> validRects;
    validRects.reserve(glyphRects.size());
    for (const auto& r : glyphRects) {
        if (!r.isEmpty()) {
            validRects.push_back(r);
        }
    }

    if (validRects.empty()) {
        return {};
    }

    // Sort by vertical center, then horizontal start
    std::sort(validRects.begin(), validRects.end(),
              [](const SelectionRect& a, const SelectionRect& b) {
                  const double midA = (a.y0 + a.y1) * 0.5;
                  const double midB = (b.y0 + b.y1) * 0.5;
                  if (std::abs(midA - midB) > 1.0) {
                      return midA < midB;
                  }
                  return a.x0 < b.x0;
              });

    // Partition into lines
    std::vector<std::vector<SelectionRect>> lines;
    for (const auto& r : validRects) {
        const double rMid = (r.y0 + r.y1) * 0.5;
        bool placed = false;
        for (auto& line : lines) {
            double lineMidSum = 0.0;
            for (const auto& item : line) {
                lineMidSum += (item.y0 + item.y1) * 0.5;
            }
            const double lineMidAvg = lineMidSum / static_cast<double>(line.size());
            if (std::abs(rMid - lineMidAvg) <= lineToleranceY) {
                line.push_back(r);
                placed = true;
                break;
            }
        }
        if (!placed) {
            lines.push_back({r});
        }
    }

    std::vector<SelectionRect> coalesced;
    coalesced.reserve(lines.size());

    for (auto& line : lines) {
        std::sort(line.begin(), line.end(),
                  [](const SelectionRect& a, const SelectionRect& b) { return a.x0 < b.x0; });

        SelectionRect current = line[0];
        constexpr double kMaxCharGap = 6.0;

        for (std::size_t i = 1; i < line.size(); ++i) {
            const auto& next = line[i];
            if (next.x0 <= current.x1 + kMaxCharGap) {
                current.x1 = std::max(current.x1, next.x1);
                current.y0 = std::min(current.y0, next.y0);
                current.y1 = std::max(current.y1, next.y1);
            } else {
                coalesced.push_back(current);
                current = next;
            }
        }
        coalesced.push_back(current);
    }

    return coalesced;
}

std::string TextSelection::formatClipboardText(const std::vector<PageTextSelection>& pages) {
    std::ostringstream ss;
    bool first = true;
    for (const auto& pageSel : pages) {
        if (pageSel.text.empty()) {
            continue;
        }
        if (!first) {
            ss << "\n\n";
        }
        ss << pageSel.text;
        first = false;
    }
    return ss.str();
}

DamageBox TextSelection::computeDamage(const SelectionRect& rect, double pageX, double pageY,
                                       double padding) {
    if (rect.isEmpty()) {
        return DamageBox{0, 0, 0, 0};
    }

    const double minX = pageX + rect.x0 - padding;
    const double minY = pageY + rect.y0 - padding;
    const double maxX = pageX + rect.x1 + padding;
    const double maxY = pageY + rect.y1 + padding;

    const int rx = std::max(0, static_cast<int>(std::floor(minX)));
    const int ry = std::max(0, static_cast<int>(std::floor(minY)));
    const int rw = std::max(0, static_cast<int>(std::ceil(maxX)) - rx);
    const int rh = std::max(0, static_cast<int>(std::ceil(maxY)) - ry);

    return DamageBox{rx, ry, rw, rh};
}

DamageBox TextSelection::computePageDamage(const PageTextSelection& pageSel, double pageX,
                                           double pageY, double padding) {
    if (pageSel.lineRects.empty()) {
        if (!pageSel.dragBounds.isEmpty()) {
            return computeDamage(pageSel.dragBounds, pageX, pageY, padding);
        }
        return DamageBox{0, 0, 0, 0};
    }

    DamageBox totalBox = computeDamage(pageSel.lineRects[0], pageX, pageY, padding);
    for (std::size_t i = 1; i < pageSel.lineRects.size(); ++i) {
        totalBox =
            uniteDamage(totalBox, computeDamage(pageSel.lineRects[i], pageX, pageY, padding));
    }
    return totalBox;
}

DamageBox TextSelection::uniteDamage(const DamageBox& a, const DamageBox& b) {
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;

    const int minX = std::min(a.x, b.x);
    const int minY = std::min(a.y, b.y);
    const int maxX = std::max(a.x + a.width, b.x + b.width);
    const int maxY = std::max(a.y + a.height, b.y + b.height);

    return DamageBox{minX, minY, std::max(0, maxX - minX), std::max(0, maxY - minY)};
}

} // namespace FluidCore
