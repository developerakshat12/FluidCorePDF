#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace FluidCoreApp {

// Pure C++20 geometry, aspect-ratio scaling, hit-testing, and scroll-synchronization
// algorithms for the document thumbnail sidebar. Free of GTK and Poppler dependencies
// to enable headless unit testing and robust layout validation.
class ThumbnailLayout {
  public:
    struct PageDimension {
        double width = 0.0;
        double height = 0.0;
        double docY = 0.0;
    };

    struct ThumbnailBox {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        double docY = 0.0;
        std::size_t pageIndex = 0;
    };

    struct LayoutConfig {
        double targetWidth = 140.0;
        double margin = 10.0;
        double gap = 14.0;
        double labelHeight = 18.0;
    };

    struct LayoutResult {
        std::vector<ThumbnailBox> boxes;
        double totalWidth = 0.0;
        double totalHeight = 0.0;
    };

    static LayoutResult computeLayout(const std::vector<PageDimension>& pages);
    static LayoutResult computeLayout(const std::vector<PageDimension>& pages,
                                      const LayoutConfig& config);

    static std::optional<std::size_t> findPageAtY(const std::vector<ThumbnailBox>& boxes,
                                                  double clickY);
    static std::optional<std::size_t> findPageAtY(const std::vector<ThumbnailBox>& boxes,
                                                  double clickY, const LayoutConfig& config);

    static std::size_t findActivePage(const std::vector<PageDimension>& pages, double viewportY,
                                      double viewportHeight);
};

inline ThumbnailLayout::LayoutResult
ThumbnailLayout::computeLayout(const std::vector<PageDimension>& pages) {
    return computeLayout(pages, LayoutConfig{});
}

inline ThumbnailLayout::LayoutResult
ThumbnailLayout::computeLayout(const std::vector<PageDimension>& pages,
                               const LayoutConfig& config) {
    LayoutResult result;
    result.totalWidth = config.targetWidth;

    if (pages.empty()) {
        result.totalHeight = 0.0;
        return result;
    }

    const double availWidth = std::max(10.0, config.targetWidth - 2.0 * config.margin);
    double currentY = config.margin;

    result.boxes.reserve(pages.size());
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& page = pages[i];
        const double pageW = page.width > 0.0 ? page.width : 1.0;
        const double pageH = page.height > 0.0 ? page.height : 1.0;

        const double scale = availWidth / pageW;
        const double thumbW = pageW * scale;
        const double thumbH = pageH * scale;
        const double x = config.margin + std::max(0.0, (availWidth - thumbW) / 2.0);

        ThumbnailBox box;
        box.x = x;
        box.y = currentY;
        box.width = thumbW;
        box.height = thumbH;
        box.docY = page.docY;
        box.pageIndex = i;

        result.boxes.push_back(box);
        currentY += thumbH + config.labelHeight + config.gap;
    }

    result.totalHeight = currentY - config.gap + config.margin;
    return result;
}

inline std::optional<std::size_t>
ThumbnailLayout::findPageAtY(const std::vector<ThumbnailBox>& boxes, double clickY) {
    return findPageAtY(boxes, clickY, LayoutConfig{});
}

inline std::optional<std::size_t>
ThumbnailLayout::findPageAtY(const std::vector<ThumbnailBox>& boxes, double clickY,
                             const LayoutConfig& config) {
    if (boxes.empty()) {
        return std::nullopt;
    }

    for (const auto& box : boxes) {
        const double top = box.y - config.gap * 0.5;
        const double bottom = box.y + box.height + config.labelHeight + config.gap * 0.5;
        if (clickY >= top && clickY <= bottom) {
            return box.pageIndex;
        }
    }

    if (clickY < boxes.front().y) {
        return boxes.front().pageIndex;
    }
    if (clickY > boxes.back().y + boxes.back().height) {
        return boxes.back().pageIndex;
    }

    return std::nullopt;
}

inline std::size_t ThumbnailLayout::findActivePage(const std::vector<PageDimension>& pages,
                                                   double viewportY, double viewportHeight) {
    if (pages.empty()) {
        return 0;
    }

    const double viewportCenter = viewportY + (viewportHeight * 0.5);
    std::size_t bestIdx = 0;
    double minDistance = std::numeric_limits<double>::infinity();

    for (std::size_t i = 0; i < pages.size(); ++i) {
        const double pageCenter = pages[i].docY + (pages[i].height * 0.5);
        const double dist = std::abs(pageCenter - viewportCenter);
        if (dist < minDistance) {
            minDistance = dist;
            bestIdx = i;
        }
    }

    return bestIdx;
}

} // namespace FluidCoreApp
