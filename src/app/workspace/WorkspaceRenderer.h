#pragma once

#include "FluidCoreAPI.h"
#include "services/ExcerptTileCache.h"
#include "workspace/WorkspaceState.h"

#include <cairo.h>

namespace FluidCoreApp {

class WorkspaceRenderer {
  public:
    static void draw(cairo_t* cr, const WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                     ExcerptTileCache* excerptTileCache, int width, int height);

    static void drawBackgroundGrid(cairo_t* cr, const WorkspaceState& state, int width, int height);
    static void drawMinimap(cairo_t* cr, const WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                            int width, int height);
    static void drawGraphEdges(cairo_t* cr, const WorkspaceState& state,
                               FluidCore::FluidCoreAPI& api);
    static void drawArrowHead(cairo_t* cr, const FluidCore::Point& tip, double angle, double size,
                              uint32_t color);

    static void drawExcerptCard(cairo_t* cr, const WorkspaceState& state,
                                const FluidCore::WorkspaceNode* node, ExcerptTileCache* tileCache,
                                double sx, double sy, double sw, double sh);
    static void drawCardStack(cairo_t* cr, const WorkspaceState& state,
                              const FluidCore::WorkspaceNode* node, ExcerptTileCache* tileCache,
                              double sx, double sy, double sw, double sh);
    static void drawGenericNode(cairo_t* cr, const WorkspaceState& state,
                                const FluidCore::WorkspaceNode* node, double sx, double sy,
                                double sw, double sh);

    static void drawMagneticSnapGuides(cairo_t* cr, const WorkspaceState& state);
    static void drawStackMergeGhost(cairo_t* cr, const WorkspaceState& state,
                                    FluidCore::FluidCoreAPI& api);

    static void drawRoundedRect(cairo_t* cr, double x, double y, double w, double h, double r);
};

} // namespace FluidCoreApp
