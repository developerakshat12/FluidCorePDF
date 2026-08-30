#include "SqueezeRenderHelper.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace FluidCoreApp {
namespace {

constexpr double kEps = 1e-9;

} // namespace

std::vector<PageSlice> SqueezeRenderHelper::decomposePage(
    std::size_t pageIndex, double pageTopDocY, double pageHeight,
    const std::vector<FluidCore::SqueezeSegment>& segments) {
    std::vector<PageSlice> slices;
    if (pageHeight <= 0.0) {
        return slices;
    }

    const double pageBottomDocY = pageTopDocY + pageHeight;

    if (segments.empty()) {
        slices.push_back(PageSlice{pageIndex, 0.0, pageHeight, pageTopDocY, pageBottomDocY,
                                   pageTopDocY, pageBottomDocY, 1.0, false});
        return slices;
    }

    for (const auto& seg : segments) {
        if (seg.docYEnd <= pageTopDocY - kEps || seg.docYStart >= pageBottomDocY + kEps) {
            continue;
        }

        const double sDocStart = std::max(pageTopDocY, seg.docYStart);
        const double sDocEnd = std::min(pageBottomDocY, seg.docYEnd);
        if (sDocEnd <= sDocStart) {
            continue;
        }

        const double sScreenStart =
            seg.screenYStart + (sDocStart - seg.docYStart) * seg.alpha;
        const double sScreenEnd =
            seg.screenYStart + (sDocEnd - seg.docYStart) * seg.alpha;

        const double localDocStart = sDocStart - pageTopDocY;
        const double localDocEnd = sDocEnd - pageTopDocY;
        const bool isCompressed = (seg.alpha < 0.999);

        slices.push_back(PageSlice{pageIndex, localDocStart, localDocEnd, sDocStart, sDocEnd,
                                   sScreenStart, sScreenEnd, seg.alpha, isCompressed});
    }

    // Guarantee exact continuity between adjacent slices
    for (std::size_t i = 0; i + 1 < slices.size(); ++i) {
        slices[i].screenYEnd = slices[i + 1].screenYStart;
        slices[i].globalDocYEnd = slices[i + 1].globalDocYStart;
        slices[i].pageLocalDocYEnd = slices[i + 1].pageLocalDocYStart;
    }

    return slices;
}

std::vector<double> SqueezeRenderHelper::extractBreakpoints(
    const std::vector<FluidCore::SqueezeSegment>& segments) {
    std::vector<double> bps;
    bps.reserve(segments.size() * 2);
    for (const auto& seg : segments) {
        bps.push_back(seg.docYStart);
        bps.push_back(seg.docYEnd);
    }
    std::sort(bps.begin(), bps.end());
    bps.erase(std::unique(bps.begin(), bps.end(),
                          [](double a, double b) { return std::abs(a - b) < kEps; }),
              bps.end());
    return bps;
}

std::vector<FluidCore::Point> SqueezeRenderHelper::subdividePointSpan(
    FluidCore::Point p1, FluidCore::Point p2, const std::vector<double>& breakpoints) {
    std::vector<FluidCore::Point> result;
    result.push_back(p1);

    const double dy = p2.y - p1.y;
    if (std::abs(dy) < kEps) {
        result.push_back(p2);
        return result;
    }

    const double minY = std::min(p1.y, p2.y);
    const double maxY = std::max(p1.y, p2.y);

    std::vector<double> crossingBps;
    for (double bp : breakpoints) {
        if (bp > minY + kEps && bp < maxY - kEps) {
            crossingBps.push_back(bp);
        }
    }

    if (p1.y < p2.y) {
        std::sort(crossingBps.begin(), crossingBps.end());
    } else {
        std::sort(crossingBps.begin(), crossingBps.end(), std::greater<double>());
    }

    for (double bp : crossingBps) {
        const double t = (bp - p1.y) / dy;
        const double interpX = p1.x + t * (p2.x - p1.x);
        result.push_back(FluidCore::Point{interpX, bp});
    }

    result.push_back(p2);
    return result;
}

std::vector<FluidCore::Rectangle> SqueezeRenderHelper::subdivideRect(
    const FluidCore::Rectangle& rect, const std::vector<double>& breakpoints) {
    std::vector<FluidCore::Rectangle> result;
    if (rect.h <= 0.0 || rect.w <= 0.0) {
        return result;
    }

    const double topY = rect.y;
    const double bottomY = rect.y + rect.h;

    std::vector<double> cuts;
    cuts.push_back(topY);
    for (double bp : breakpoints) {
        if (bp > topY + kEps && bp < bottomY - kEps) {
            cuts.push_back(bp);
        }
    }
    cuts.push_back(bottomY);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(),
                           [](double a, double b) { return std::abs(a - b) < kEps; }),
               cuts.end());

    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const double h = cuts[i + 1] - cuts[i];
        if (h > kEps) {
            result.push_back(FluidCore::Rectangle{rect.x, cuts[i], rect.w, h});
        }
    }

    return result;
}

void SqueezeRenderHelper::renderAccordionCrease(cairo_t* cr, double x, double screenY,
                                                double width, double alpha) {
    if (width <= 0.0) {
        return;
    }

    cairo_save(cr);

    // Subtle dual-gradient shadow band indicating folded compression
    const double shadowHeight = std::clamp(6.0 * (1.0 - alpha), 2.0, 8.0);

    // Top shadow
    cairo_pattern_t* topPattern =
        cairo_pattern_create_linear(x, screenY - shadowHeight, x, screenY);
    cairo_pattern_add_color_stop_rgba(topPattern, 0.0, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(topPattern, 1.0, 0.0, 0.0, 0.0, 0.18);
    cairo_set_source(cr, topPattern);
    cairo_rectangle(cr, x, screenY - shadowHeight, width, shadowHeight);
    cairo_fill(cr);
    cairo_pattern_destroy(topPattern);

    // Bottom shadow
    cairo_pattern_t* botPattern =
        cairo_pattern_create_linear(x, screenY, x, screenY + shadowHeight);
    cairo_pattern_add_color_stop_rgba(botPattern, 0.0, 0.0, 0.0, 0.0, 0.18);
    cairo_pattern_add_color_stop_rgba(botPattern, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, botPattern);
    cairo_rectangle(cr, x, screenY, width, shadowHeight);
    cairo_fill(cr);
    cairo_pattern_destroy(botPattern);

    // Crease fold centerline rule
    cairo_set_source_rgba(cr, 0.25, 0.45, 0.75, 0.85);
    cairo_set_line_width(cr, 1.0);
    const double dashes[] = {4.0, 3.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_move_to(cr, x, screenY);
    cairo_line_to(cr, x + width, screenY);
    cairo_stroke(cr);

    cairo_restore(cr);
}

void SqueezeRenderHelper::renderMarginFoldPin(cairo_t* cr, double pinX, double screenY,
                                              double radius, bool isHovered, bool isDragging) {
    cairo_save(cr);

    // Pin shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.20);
    cairo_arc(cr, pinX, screenY + 1.0, radius, 0.0, 2.0 * M_PI);
    cairo_fill(cr);

    // Pin outer badge fill
    if (isDragging) {
        cairo_set_source_rgb(cr, 0.15, 0.40, 0.85); // Active blue
    } else if (isHovered) {
        cairo_set_source_rgb(cr, 0.30, 0.60, 0.95); // Hover light blue
    } else {
        cairo_set_source_rgb(cr, 0.45, 0.50, 0.60); // Neutral slate
    }
    cairo_arc(cr, pinX, screenY, radius, 0.0, 2.0 * M_PI);
    cairo_fill_preserve(cr);

    // Pin border outline
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    // Inner grip center dot
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_arc(cr, pinX, screenY, radius * 0.35, 0.0, 2.0 * M_PI);
    cairo_fill(cr);

    cairo_restore(cr);
}

} // namespace FluidCoreApp
