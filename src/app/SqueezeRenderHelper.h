#pragma once

#include "FluidCoreAPI.h"
#include "squeeze/SqueezeEngine.h"

#include <cairo.h>

#include <cstddef>
#include <vector>

namespace FluidCoreApp {

struct PageSlice {
    std::size_t pageIndex = 0;
    double pageLocalDocYStart = 0.0;
    double pageLocalDocYEnd = 0.0;
    double globalDocYStart = 0.0;
    double globalDocYEnd = 0.0;
    double screenYStart = 0.0;
    double screenYEnd = 0.0;
    double alpha = 1.0;
    bool isCompressed = false;
};

class SqueezeRenderHelper {
  public:
    // Decomposes a page into contiguous horizontal slices matching the SqueezeEngine segments.
    // Enforces pixel-snapping and exact boundary continuity: slice[k].screenYEnd ==
    // slice[k+1].screenYStart.
    static std::vector<PageSlice>
    decomposePage(std::size_t pageIndex, double pageTopDocY, double pageHeight,
                  const std::vector<FluidCore::SqueezeSegment>& segments);

    // Extracts all active document-space horizontal breakpoint Y-coordinates from segments.
    static std::vector<double>
    extractBreakpoints(const std::vector<FluidCore::SqueezeSegment>& segments);

    // Subdivides a vector line segment [P1, P2] at every intermediate squeeze breakpoint.
    // Prevents angular kinks and geometric distortion when strokes cross accordion creases.
    static std::vector<FluidCore::Point> subdividePointSpan(FluidCore::Point p1,
                                                            FluidCore::Point p2,
                                                            const std::vector<double>& breakpoints);

    // Subdivides a rectangle vertically at every intermediate squeeze breakpoint.
    static std::vector<FluidCore::Rectangle> subdivideRect(const FluidCore::Rectangle& rect,
                                                           const std::vector<double>& breakpoints);

    // Renders visual accordion fold crease lines and shadows at compression boundaries.
    static void renderAccordionCrease(cairo_t* cr, double x, double screenY, double width,
                                      double alpha);

    // Renders margin fold pin indicators for interactive crease manipulation.
    static void renderMarginFoldPin(cairo_t* cr, double pinX, double screenY, double radius,
                                    bool isHovered, bool isDragging);
};

} // namespace FluidCoreApp
