#include "workspace/WorkspaceRenderer.h"
#include "FluidCoreEngine.h"
#include "graph/GraphTopology.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/CardLayoutEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceInteraction.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace FluidCoreApp {

constexpr double kBaseGridStep = 32.0;
constexpr double kMajorGridMultiple = 5.0;

void WorkspaceRenderer::drawRoundedRect(cairo_t* cr, double x, double y, double w, double h,
                                        double r) {
    if (w < 2.0 * r)
        r = w / 2.0;
    if (h < 2.0 * r)
        r = h / 2.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

void WorkspaceRenderer::drawBackgroundGrid(cairo_t* cr, const WorkspaceState& state, int width,
                                           int height) {
    const double zoom = state.viewport.zoom;
    const double originX = state.viewport.originX;
    const double originY = state.viewport.originY;

    double dynamicGridStep = kBaseGridStep * zoom;
    while (dynamicGridStep < 16.0)
        dynamicGridStep *= 2.0;
    while (dynamicGridStep > 64.0)
        dynamicGridStep /= 2.0;

    const double worldGridStep = dynamicGridStep / zoom;
    const double startWorldX = std::floor(originX / worldGridStep) * worldGridStep;
    const double startWorldY = std::floor(originY / worldGridStep) * worldGridStep;

    const double screenStartX = (startWorldX - originX) * zoom;
    const double screenStartY = (startWorldY - originY) * zoom;
    const double dotRadius = std::clamp(1.1 * std::min(1.0, zoom), 0.7, 1.8);

    cairo_set_source_rgb(cr, 0.78, 0.83, 0.89);
    for (double sx = screenStartX - dynamicGridStep; sx < width + dynamicGridStep;
         sx += dynamicGridStep) {
        for (double sy = screenStartY - dynamicGridStep; sy < height + dynamicGridStep;
             sy += dynamicGridStep) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, sx, sy, dotRadius, 0, 2 * M_PI);
        }
    }
    cairo_fill(cr);

    if (zoom >= 0.3) {
        const double majorStep = dynamicGridStep * kMajorGridMultiple;
        const double mStartX = std::fmod(screenStartX, majorStep);
        const double mStartY = std::fmod(screenStartY, majorStep);

        cairo_set_source_rgb(cr, 0.65, 0.72, 0.80);
        cairo_set_line_width(cr, 1.2);
        for (double x = mStartX - majorStep; x < width; x += majorStep) {
            for (double y = mStartY - majorStep; y < height; y += majorStep) {
                cairo_move_to(cr, x - 3.0, y);
                cairo_line_to(cr, x + 3.0, y);
                cairo_move_to(cr, x, y - 3.0);
                cairo_line_to(cr, x, y + 3.0);
            }
        }
        cairo_stroke(cr);
    }
}

void WorkspaceRenderer::drawMinimap(cairo_t* cr, const WorkspaceState& state,
                                    FluidCore::FluidCoreAPI& api, int width, int height) {
    if (!state.showMinimap)
        return;

    const FluidCore::Rectangle mm = WorkspaceInteraction::getMinimapRect(state, width, height);
    if (mm.w <= 40.0 || mm.h <= 30.0)
        return;

    cairo_save(cr);

    // Soft drop shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.08);
    drawRoundedRect(cr, mm.x + 2.0, mm.y + 4.0, mm.w, mm.h, 8.0);
    cairo_fill(cr);

    // Frosted card background
    drawRoundedRect(cr, mm.x, mm.y, mm.w, mm.h, 8.0);
    cairo_set_source_rgba(cr, 0.98, 0.99, 1.0, 0.92);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.75, 0.82, 0.90, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    FluidCore::Rectangle wsBounds = api.getWorkspaceBounds();
    const double currentViewW = width / state.viewport.zoom;
    const double currentViewH = height / state.viewport.zoom;

    if (wsBounds.w <= 0.0 || wsBounds.h <= 0.0) {
        wsBounds = {state.viewport.originX, state.viewport.originY, currentViewW, currentViewH};
    } else {
        const double minX = std::min(wsBounds.x, state.viewport.originX);
        const double minY = std::min(wsBounds.y, state.viewport.originY);
        const double maxX =
            std::max(wsBounds.x + wsBounds.w, state.viewport.originX + currentViewW);
        const double maxY =
            std::max(wsBounds.y + wsBounds.h, state.viewport.originY + currentViewH);
        wsBounds = {minX, minY, maxX - minX, maxY - minY};
    }

    const double padX = std::max(wsBounds.w * 0.1, 100.0);
    const double padY = std::max(wsBounds.h * 0.1, 100.0);
    const double mapWorldX = wsBounds.x - padX;
    const double mapWorldY = wsBounds.y - padY;
    const double mapWorldW = wsBounds.w + padX * 2.0;
    const double mapWorldH = wsBounds.h + padY * 2.0;

    auto worldToMinimap = [&](double wx, double wy) -> FluidCore::Point {
        const double nx = (wx - mapWorldX) / mapWorldW;
        const double ny = (wy - mapWorldY) / mapWorldH;
        return {mm.x + 8.0 + nx * (mm.w - 16.0), mm.y + 24.0 + ny * (mm.h - 32.0)};
    };

    cairo_save(cr);
    drawRoundedRect(cr, mm.x + 1.0, mm.y + 1.0, mm.w - 2.0, mm.h - 2.0, 7.0);
    cairo_clip(cr);

    // Miniature nodes
    const FluidCore::Rectangle queryAll{mapWorldX, mapWorldY, mapWorldW, mapWorldH};
    cairo_set_source_rgba(cr, 0.45, 0.58, 0.75, 0.7);
    for (const FluidCore::WorkspaceNode* node : api.queryVisibleNodes(queryAll)) {
        const FluidCore::Rectangle b = node->bounds();
        const FluidCore::Point p1 = worldToMinimap(b.x, b.y);
        const FluidCore::Point p2 = worldToMinimap(b.x + b.w, b.y + b.h);
        const double mw = std::max(p2.x - p1.x, 3.0);
        const double mh = std::max(p2.y - p1.y, 3.0);

        drawRoundedRect(cr, p1.x, p1.y, mw, mh, 2.0);
        cairo_fill(cr);
    }

    // Glowing amber search hits on minimap (TASK-4.3)
    if (state.search.active && !state.search.matches.empty()) {
        cairo_set_source_rgba(cr, 0.96, 0.62, 0.07, 0.95);
        for (const auto& match : state.search.matches) {
            const FluidCore::Point mp = worldToMinimap(match.bounds.x + match.bounds.w / 2.0,
                                                       match.bounds.y + match.bounds.h / 2.0);
            cairo_arc(cr, mp.x, mp.y, 3.5, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    // Active viewport frame indicator
    const FluidCore::Point vp1 = worldToMinimap(state.viewport.originX, state.viewport.originY);
    const FluidCore::Point vp2 = worldToMinimap(state.viewport.originX + currentViewW,
                                                state.viewport.originY + currentViewH);
    const double vpw = std::max(vp2.x - vp1.x, 6.0);
    const double vph = std::max(vp2.y - vp1.y, 6.0);

    drawRoundedRect(cr, vp1.x, vp1.y, vpw, vph, 3.0);
    cairo_set_source_rgba(cr, 0.01, 0.52, 0.78, 0.18);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.01, 0.52, 0.78, 0.90);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    cairo_restore(cr);

    // Header label & Zoom badge
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9.0);
    cairo_set_source_rgb(cr, 0.35, 0.45, 0.55);
    cairo_move_to(cr, mm.x + 8.0, mm.y + 14.0);
    cairo_show_text(cr, "OVERVIEW");

    std::ostringstream oss;
    oss << static_cast<int>(std::round(state.viewport.zoom * 100.0)) << "%";
    const std::string zoomStr = oss.str();
    cairo_text_extents_t ext;
    cairo_text_extents(cr, zoomStr.c_str(), &ext);
    cairo_move_to(cr, mm.x + mm.w - ext.width - 8.0, mm.y + 14.0);
    cairo_show_text(cr, zoomStr.c_str());

    cairo_restore(cr);
}

void WorkspaceRenderer::drawArrowHead(cairo_t* cr, const FluidCore::Point& tip, double angle,
                                      double size, uint32_t color) {
    const double arrowAngle = M_PI / 6.0;
    const double p1X = tip.x - size * std::cos(angle - arrowAngle);
    const double p1Y = tip.y - size * std::sin(angle - arrowAngle);
    const double p2X = tip.x - size * std::cos(angle + arrowAngle);
    const double p2Y = tip.y - size * std::sin(angle + arrowAngle);

    cairo_save(cr);
    cairo_set_source_rgba(cr, ((color >> 16) & 0xFF) / 255.0, ((color >> 8) & 0xFF) / 255.0,
                          (color & 0xFF) / 255.0, 1.0);
    cairo_move_to(cr, tip.x, tip.y);
    cairo_line_to(cr, p1X, p1Y);
    cairo_line_to(cr, p2X, p2Y);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
}

void WorkspaceRenderer::drawGraphEdges(cairo_t* cr, const WorkspaceState& state,
                                       FluidCore::FluidCoreAPI& api) {
    auto* engine = dynamic_cast<FluidCore::FluidCoreEngine*>(&api);
    std::vector<std::string> allEdges = api.getAllEdges();
    const double zoom = state.viewport.zoom;

    for (const auto& edgeId : allEdges) {
        FluidCore::BezierSpline spline = api.getEdgeGeometry(edgeId);
        if (spline.controlPoints.size() < 4) {
            continue;
        }

        const auto& p0W = spline.controlPoints[0];
        const auto& p1W = spline.controlPoints[1];
        const auto& p2W = spline.controlPoints[2];
        const auto& p3W = spline.controlPoints[3];

        const FluidCore::Point s0 = state.viewport.worldToScreen(p0W.x, p0W.y);
        const FluidCore::Point s1 = state.viewport.worldToScreen(p1W.x, p1W.y);
        const FluidCore::Point s2 = state.viewport.worldToScreen(p2W.x, p2W.y);
        const FluidCore::Point s3 = state.viewport.worldToScreen(p3W.x, p3W.y);

        const bool isSelected = (state.selectedEdgeId && *state.selectedEdgeId == edgeId);

        if (isSelected) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.35);
            cairo_set_line_width(cr, std::max(6.0, 8.0 * zoom));
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_move_to(cr, s0.x, s0.y);
            cairo_curve_to(cr, s1.x, s1.y, s2.x, s2.y, s3.x, s3.y);
            cairo_stroke(cr);
            cairo_restore(cr);
        }

        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.08);
        cairo_set_line_width(cr, std::max(2.5, 3.5 * zoom));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, s0.x, s0.y + 1.5 * zoom);
        cairo_curve_to(cr, s1.x, s1.y + 1.5 * zoom, s2.x, s2.y + 1.5 * zoom, s3.x,
                       s3.y + 1.5 * zoom);
        cairo_stroke(cr);
        cairo_restore(cr);

        cairo_save(cr);
        if (isSelected) {
            cairo_set_source_rgb(cr, 0.02, 0.45, 0.90);
            cairo_set_line_width(cr, std::max(2.2, 3.0 * zoom));
        } else {
            cairo_set_source_rgb(cr, 0.12, 0.50, 0.95);
            cairo_set_line_width(cr, std::max(1.5, 2.2 * zoom));
        }
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, s0.x, s0.y);
        cairo_curve_to(cr, s1.x, s1.y, s2.x, s2.y, s3.x, s3.y);
        cairo_stroke(cr);

        bool isBidirectional = false;
        if (engine) {
            auto edgeOpt = engine->graphTopology().findEdge(edgeId);
            if (edgeOpt && edgeOpt->direction == FluidCore::EdgeDirection::Bidirectional) {
                isBidirectional = true;
            }
        }

        const double arrowSize = std::clamp(12.0 * zoom, 7.0, 20.0);

        double tangentTargetX = s3.x - s2.x;
        double tangentTargetY = s3.y - s2.y;
        if (std::abs(tangentTargetX) < 1e-6 && std::abs(tangentTargetY) < 1e-6) {
            tangentTargetX = s3.x - s0.x;
            tangentTargetY = s3.y - s0.y;
        }
        const double arrivalAngle = std::atan2(tangentTargetY, tangentTargetX);
        drawArrowHead(cr, s3, arrivalAngle, arrowSize, isSelected ? 0x0374B5 : 0x1E88E5);

        if (isBidirectional) {
            double tangentSourceX = s0.x - s1.x;
            double tangentSourceY = s0.y - s1.y;
            if (std::abs(tangentSourceX) < 1e-6 && std::abs(tangentSourceY) < 1e-6) {
                tangentSourceX = s0.x - s3.x;
                tangentSourceY = s0.y - s3.y;
            }
            const double departureAngle = std::atan2(tangentSourceY, tangentSourceX);
            drawArrowHead(cr, s0, departureAngle, arrowSize, isSelected ? 0x0374B5 : 0x1E88E5);
        }

        cairo_restore(cr);
    }

    if (state.connector.isConnecting) {
        const FluidCore::Point sStart = state.viewport.worldToScreen(
            state.connector.connectorStartWorld.x, state.connector.connectorStartWorld.y);
        const FluidCore::Point sCur = state.viewport.worldToScreen(
            state.connector.connectorCurrentWorld.x, state.connector.connectorCurrentWorld.y);

        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.12, 0.55, 0.95, 0.85);
        double dashes[] = {6.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_set_line_width(cr, std::max(1.8, 2.5 * zoom));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, sStart.x, sStart.y);
        cairo_line_to(cr, sCur.x, sCur.y);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0.0);

        const double angle = std::atan2(sCur.y - sStart.y, sCur.x - sStart.x);
        const double arrowSize = std::clamp(12.0 * zoom, 7.0, 20.0);
        drawArrowHead(cr, sCur, angle, arrowSize, 0x1E88E5);
        cairo_restore(cr);
    }
}

void WorkspaceRenderer::drawExcerptCard(cairo_t* cr, const WorkspaceState& state,
                                        const FluidCore::WorkspaceNode* node,
                                        ExcerptTileCache* tileCache, double sx, double sy,
                                        double sw, double sh) {
    const auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node);
    const double zoom = state.viewport.zoom;
    const double radius = 8.0 * zoom;

    // 1. Soft layered elevation card shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.04);
    drawRoundedRect(cr, sx, sy + 3.0 * zoom, sw, sh, radius);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx, sy + 1.0 * zoom, sw, sh, radius);
    cairo_fill(cr);

    // 2. Card background container
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);

    // 3. Card border
    cairo_set_source_rgba(cr, 0.82, 0.86, 0.92, 0.95);
    cairo_set_line_width(cr, std::max(1.0, 1.0 * zoom));
    cairo_stroke(cr);

    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_clip(cr);

    const double headerH = std::min(28.0 * zoom, sh * 0.35);

    // 4. Header background bar
    cairo_rectangle(cr, sx, sy, sw, headerH);
    cairo_set_source_rgb(cr, 0.965, 0.975, 0.99);
    cairo_fill(cr);

    // Header divider line
    cairo_move_to(cr, sx, sy + headerH);
    cairo_line_to(cr, sx + sw, sy + headerH);
    cairo_set_source_rgba(cr, 0.88, 0.91, 0.95, 0.9);
    cairo_set_line_width(cr, std::max(0.75, 0.85 * zoom));
    cairo_stroke(cr);

    const double anchorW = 16.0 * zoom;
    const bool isHovered = (excerpt && excerpt->id() == state.hoveredAnchorCardId);

    // 5. Left Anchor bar with clickable arrow indicator
    if (excerpt) {
        const auto col = excerpt->color();
        // Anchor bar background
        cairo_rectangle(cr, sx, sy, anchorW, sh);
        if (isHovered) {
            cairo_set_source_rgba(cr, col.r / 255.0, col.g / 255.0, col.b / 255.0, 1.0);
        } else {
            cairo_set_source_rgba(cr, col.r / 255.0, col.g / 255.0, col.b / 255.0, 0.90);
        }
        cairo_fill(cr);

        // Subtle divider separating anchor bar from card content
        cairo_move_to(cr, sx + anchorW, sy);
        cairo_line_to(cr, sx + anchorW, sy + sh);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.15);
        cairo_set_line_width(cr, std::max(0.5, 0.75 * zoom));
        cairo_stroke(cr);

        // Clickable arrow indicator pointing left towards the source document
        if (zoom >= 0.2) {
            const double arrowW = 6.0 * zoom;
            const double arrowH = 9.0 * zoom;
            const double nudge = isHovered ? -1.2 * zoom : 0.0;
            const double arrowCenterX = sx + anchorW / 2.0 + nudge;
            const double arrowCenterY = sy + sh / 2.0;

            cairo_move_to(cr, arrowCenterX - arrowW * 0.5, arrowCenterY);
            cairo_line_to(cr, arrowCenterX + arrowW * 0.5, arrowCenterY - arrowH * 0.5);
            cairo_line_to(cr, arrowCenterX + arrowW * 0.5, arrowCenterY + arrowH * 0.5);
            cairo_close_path(cr);

            // Contrast-aware arrow coloring based on background luminance
            const double lum =
                0.299 * (col.r / 255.0) + 0.587 * (col.g / 255.0) + 0.114 * (col.b / 255.0);
            if (lum > 0.75) {
                cairo_set_source_rgba(cr, 0.15, 0.20, 0.28, isHovered ? 1.0 : 0.85);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, isHovered ? 1.0 : 0.90);
            }
            cairo_fill(cr);
        }
    }

    if (zoom >= 0.2) {
        std::string docLabel = "Document";
        size_t pageNum = 1;
        if (excerpt) {
            std::string path = excerpt->sourceDocId();
            size_t slash = path.find_last_of("/\\");
            if (slash != std::string::npos && slash + 1 < path.size()) {
                docLabel = path.substr(slash + 1);
            } else if (!path.empty()) {
                docLabel = path;
            }
            pageNum = excerpt->sourcePageNo() + 1;
        } else {
            docLabel = node->id();
        }

        std::ostringstream headerOss;
        if (excerpt) {
            headerOss << docLabel << " • Page " << pageNum;
        } else {
            headerOss << docLabel;
        }
        std::string headerStr = headerOss.str();

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 10.5 * zoom);
        cairo_set_source_rgb(cr, 0.22, 0.28, 0.38);
        cairo_move_to(cr, sx + anchorW + 10.0 * zoom, sy + headerH * 0.67);
        cairo_show_text(cr, headerStr.c_str());
    }

    // Return focus flash aura
    if (state.animation.flashAlpha > 0.01 && excerpt &&
        excerpt->id() == state.animation.flashCardId) {
        cairo_save(cr);
        drawRoundedRect(cr, sx - 4.0, sy - 4.0, sw + 8.0, sh + 8.0, radius + 4.0);
        cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.35 * state.animation.flashAlpha);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.90 * state.animation.flashAlpha);
        cairo_set_line_width(cr, 2.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Workspace Canvas Find search match aura (TASK-4.3)
    drawSearchAura(cr, state, node, sx, sy, sw, sh, radius);

    // Body Content Rendering
    if (excerpt) {
        if (excerpt->isImageExcerpt()) {
            const double bodyX = sx + anchorW + 8.0 * zoom;
            const double bodyY = sy + headerH + 6.0 * zoom;
            const double bodyW = sw - anchorW - 16.0 * zoom;
            const double bodyH = sh - headerH - 12.0 * zoom;

            if (bodyW > 8.0 && bodyH > 8.0) {
                CairoSurfaceHandle surface;
                if (tileCache) {
                    LodTier tier = computeLodTierFromZoom(zoom);
                    CropCacheKey key = CropCacheKey::fromNormalizedRect(
                        excerpt->sourceDocId(), excerpt->sourcePageNo(),
                        excerpt->sourceNormalizedRect(), tier);
                    surface = tileCache->get(key);
                    if (!surface) {
                        surface = tileCache->getBestAvailableSurface(
                            excerpt->sourceDocId(), excerpt->sourcePageNo(),
                            excerpt->sourceNormalizedRect());
                        tileCache->requestCropAsync(
                            excerpt->id(), excerpt->sourceDocId(), excerpt->sourcePageNo(),
                            excerpt->sourceNormalizedRect(), excerpt->bounds().w - 20.0,
                            excerpt->bounds().h - 36.0, zoom);
                    }
                }

                if (surface) {
                    drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                    cairo_save(cr);
                    cairo_clip(cr);

                    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                    cairo_rectangle(cr, bodyX, bodyY, bodyW, bodyH);
                    cairo_fill(cr);

                    double surfW = surface.width();
                    double surfH = surface.height();
                    if (surfW > 0.0 && surfH > 0.0) {
                        double scale = std::min(bodyW / surfW, bodyH / surfH);
                        double destW = surfW * scale;
                        double destH = surfH * scale;
                        double destX = bodyX + (bodyW - destW) / 2.0;
                        double destY = bodyY + (bodyH - destH) / 2.0;

                        cairo_save(cr);
                        cairo_translate(cr, destX, destY);
                        cairo_scale(cr, scale, scale);
                        cairo_set_source_surface(cr, surface.get(), 0, 0);
                        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
                        cairo_paint(cr);
                        cairo_restore(cr);
                    }

                    cairo_restore(cr);

                    drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                    cairo_set_source_rgba(cr, 0.75, 0.82, 0.90, 0.9);
                    cairo_set_line_width(cr, 1.0);
                    cairo_stroke(cr);
                } else {
                    drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                    cairo_set_source_rgb(cr, 0.94, 0.96, 0.99);
                    cairo_fill_preserve(cr);

                    cairo_set_source_rgb(cr, 0.75, 0.80, 0.88);
                    double dashes[] = {4.0, 4.0};
                    cairo_set_dash(cr, dashes, 2, 0.0);
                    cairo_set_line_width(cr, 1.0);
                    cairo_stroke(cr);
                    cairo_set_dash(cr, nullptr, 0, 0.0);

                    if (zoom >= 0.25) {
                        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                               CAIRO_FONT_WEIGHT_BOLD);
                        cairo_set_font_size(cr, 10.0 * zoom);
                        cairo_set_source_rgb(cr, 0.45, 0.52, 0.62);
                        cairo_text_extents_t ext;
                        cairo_text_extents(cr, "Visual Diagram Crop", &ext);
                        cairo_move_to(cr, bodyX + (bodyW - ext.width) / 2.0,
                                      bodyY + (bodyH + ext.height) / 2.0);
                        cairo_show_text(cr, "Visual Diagram Crop");
                    }
                }
            }
        } else {
            if (zoom < 0.25) {
                const double barStartX = sx + anchorW + 8.0 * zoom;
                const double barW = sw - anchorW - 16.0 * zoom;
                double curY = sy + headerH + 6.0 * zoom;
                cairo_set_source_rgba(cr, 0.70, 0.75, 0.83, 0.7);
                for (int i = 0; i < 4 && curY + 4.0 * zoom < sy + sh - 4.0 * zoom; ++i) {
                    double wFraction = (i == 3) ? 0.6 : ((i % 2 == 0) ? 0.95 : 0.85);
                    cairo_rectangle(cr, barStartX, curY, barW * wFraction, 2.5 * zoom);
                    cairo_fill(cr);
                    curY += 5.0 * zoom;
                }
            } else {
                const double textStartX = sx + anchorW + 10.0 * zoom;
                const double maxW = sw - anchorW - 20.0 * zoom;
                const double fontSize = 11.5 * zoom;
                const double lineSpacing = 16.5 * zoom;
                double curY = sy + headerH + 18.0 * zoom;

                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, fontSize);
                cairo_set_source_rgb(cr, 0.15, 0.20, 0.28);

                const std::string& snippet = excerpt->textSnippet();
                std::istringstream stream(snippet);
                std::string line;

                while (std::getline(stream, line)) {
                    if (curY + lineSpacing > sy + sh) {
                        cairo_move_to(cr, textStartX, curY);
                        cairo_show_text(cr, "...");
                        break;
                    }

                    std::istringstream wordStream(line);
                    std::string word;
                    std::string currentLine;

                    while (wordStream >> word) {
                        std::string testLine =
                            currentLine.empty() ? word : currentLine + " " + word;
                        cairo_text_extents_t ext;
                        cairo_text_extents(cr, testLine.c_str(), &ext);

                        if (ext.width > maxW && !currentLine.empty()) {
                            if (curY + lineSpacing > sy + sh) {
                                cairo_move_to(cr, textStartX, curY);
                                cairo_show_text(cr, (currentLine + "...").c_str());
                                currentLine.clear();
                                break;
                            }
                            cairo_move_to(cr, textStartX, curY);
                            cairo_show_text(cr, currentLine.c_str());
                            curY += lineSpacing;
                            currentLine = word;
                        } else {
                            currentLine = testLine;
                        }
                    }

                    if (!currentLine.empty()) {
                        cairo_move_to(cr, textStartX, curY);
                        cairo_show_text(cr, currentLine.c_str());
                        curY += lineSpacing;
                    }
                }
            }
        }
    }

    cairo_restore(cr);
}

void WorkspaceRenderer::drawGenericNode(cairo_t* cr, const WorkspaceState& state,
                                        const FluidCore::WorkspaceNode* node, double sx, double sy,
                                        double sw, double sh) {
    const double zoom = state.viewport.zoom;
    const double radius = 8.0 * zoom;

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.04);
    drawRoundedRect(cr, sx, sy + 3.0 * zoom, sw, sh, radius);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx, sy + 1.0 * zoom, sw, sh, radius);
    cairo_fill(cr);

    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.82, 0.86, 0.92, 0.95);
    cairo_set_line_width(cr, std::max(1.0, 1.0 * zoom));
    cairo_stroke(cr);

    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_clip(cr);
    cairo_rectangle(cr, sx, sy, sw, 5.0 * zoom);
    cairo_set_source_rgb(cr, 0.20, 0.55, 0.90);
    cairo_fill(cr);

    if (zoom >= 0.25) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0 * zoom);
        cairo_set_source_rgb(cr, 0.20, 0.26, 0.35);
        cairo_move_to(cr, sx + 14.0 * zoom, sy + 24.0 * zoom);
        cairo_show_text(cr, node->id().c_str());
    }
    cairo_restore(cr);

    // Workspace Canvas Find search match aura (TASK-4.3)
    drawSearchAura(cr, state, node, sx, sy, sw, sh, radius);
}

void WorkspaceRenderer::drawCardStack(cairo_t* cr, const WorkspaceState& state,
                                      const FluidCore::WorkspaceNode* node,
                                      ExcerptTileCache* tileCache, double sx, double sy, double sw,
                                      double sh) {
    const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node);
    if (!stack)
        return;

    const double zoom = state.viewport.zoom;
    const double radius = 8.0 * zoom;
    const double headerH = FluidCore::CardStackNode::kHeaderHeight * zoom;
    const bool isCollapsed = stack->isCollapsed();
    const bool isSelected = (state.selectedNodeId && *state.selectedNodeId == stack->id());

    if (isCollapsed) {
        for (int i = 2; i >= 1; --i) {
            const double offset = static_cast<double>(i) * 3.5 * zoom;
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.04);
            drawRoundedRect(cr, sx + offset, sy + offset, sw - offset * 2.0, sh, radius);
            cairo_fill(cr);

            drawRoundedRect(cr, sx + offset * 0.5, sy + offset, sw - offset, sh, radius);
            cairo_set_source_rgb(cr, 0.93 - i * 0.03, 0.94 - i * 0.03, 0.96 - i * 0.03);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.80, 0.84, 0.90, 0.7);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }
    }

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx, sy + 4.0 * zoom, sw, sh, radius);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.08);
    drawRoundedRect(cr, sx, sy + 1.5 * zoom, sw, sh, radius);
    cairo_fill(cr);

    if (isSelected) {
        cairo_save(cr);
        drawRoundedRect(cr, sx - 3.0, sy - 3.0, sw + 6.0, sh + 6.0, radius + 2.0);
        cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.35);
        cairo_set_line_width(cr, 2.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 0.985, 0.988, 0.995);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.76, 0.82, 0.90, 0.95);
    cairo_set_line_width(cr, std::max(1.0, 1.2 * zoom));
    cairo_stroke(cr);

    if (!isCollapsed) {
        for (const auto& child : stack->children()) {
            if (!child)
                continue;
            const auto cb = child->bounds();
            const double csx = (cb.x - state.viewport.originX) * zoom;
            const double csy = (cb.y - state.viewport.originY) * zoom;
            const double csw = cb.w * zoom;
            const double csh = cb.h * zoom;

            if (dynamic_cast<const FluidCore::ExcerptCardNode*>(child.get())) {
                drawExcerptCard(cr, state, child.get(), tileCache, csx, csy, csw, csh);
            } else if (dynamic_cast<const FluidCore::CardStackNode*>(child.get())) {
                drawCardStack(cr, state, child.get(), tileCache, csx, csy, csw, csh);
            } else {
                drawGenericNode(cr, state, child.get(), csx, csy, csw, csh);
            }
        }
    }

    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, headerH, radius);
    cairo_clip(cr);

    cairo_pattern_t* grad = cairo_pattern_create_linear(sx, sy, sx, sy + headerH);
    cairo_pattern_add_color_stop_rgb(grad, 0.0, 0.16, 0.20, 0.28);
    cairo_pattern_add_color_stop_rgb(grad, 1.0, 0.11, 0.14, 0.20);
    cairo_set_source(cr, grad);
    cairo_rectangle(cr, sx, sy, sw, headerH);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    cairo_move_to(cr, sx, sy + headerH);
    cairo_line_to(cr, sx + sw, sy + headerH);
    cairo_set_source_rgba(cr, 0.28, 0.35, 0.48, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    const double chevronSize = std::min(18.0 * zoom, headerH * 0.7);
    const double chevronX = sx + 8.0 * zoom;
    const double chevronY = sy + (headerH - chevronSize) / 2.0;

    drawRoundedRect(cr, chevronX, chevronY, chevronSize, chevronSize, chevronSize / 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.90, 0.94, 1.0);
    if (!isCollapsed) {
        const double cx = chevronX + chevronSize / 2.0;
        const double cy = chevronY + chevronSize / 2.0;
        const double triR = chevronSize * 0.25;
        cairo_move_to(cr, cx - triR, cy - triR * 0.6);
        cairo_line_to(cr, cx + triR, cy - triR * 0.6);
        cairo_line_to(cr, cx, cy + triR * 0.8);
        cairo_close_path(cr);
        cairo_fill(cr);
    } else {
        const double cx = chevronX + chevronSize / 2.0;
        const double cy = chevronY + chevronSize / 2.0;
        const double triR = chevronSize * 0.25;
        cairo_move_to(cr, cx - triR * 0.6, cy - triR);
        cairo_line_to(cr, cx + triR * 0.8, cy);
        cairo_line_to(cr, cx - triR * 0.6, cy + triR);
        cairo_close_path(cr);
        cairo_fill(cr);
    }

    if (zoom >= 0.2) {
        std::string countStr = std::to_string(stack->childCount()) + " cards";
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9.0 * zoom);
        cairo_text_extents_t cntExt;
        cairo_text_extents(cr, countStr.c_str(), &cntExt);

        const double badgeW = cntExt.width + 12.0 * zoom;
        const double badgeH = 18.0 * zoom;
        const double badgeX = sx + sw - badgeW - 8.0 * zoom;
        const double badgeY = sy + (headerH - badgeH) / 2.0;

        const double titleStartX = chevronX + chevronSize + 8.0 * zoom;
        double maxTitleW = (badgeX > titleStartX + 40.0 * zoom)
                               ? (badgeX - titleStartX - 10.0 * zoom)
                               : (sx + sw - titleStartX - 10.0 * zoom);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0 * zoom);
        cairo_set_source_rgb(cr, 0.95, 0.97, 1.0);

        std::string displayTitle = stack->title();
        cairo_text_extents_t titleExt;
        cairo_text_extents(cr, displayTitle.c_str(), &titleExt);

        if (titleExt.width > maxTitleW && maxTitleW > 20.0 * zoom) {
            while (!displayTitle.empty() && titleExt.width > maxTitleW) {
                displayTitle.pop_back();
                std::string candidate = displayTitle + "...";
                cairo_text_extents(cr, candidate.c_str(), &titleExt);
            }
            displayTitle += "...";
        }

        cairo_move_to(cr, titleStartX, sy + headerH * 0.65);
        cairo_show_text(cr, displayTitle.c_str());

        if (badgeW > 10.0 && badgeX > titleStartX + 40.0 * zoom) {
            drawRoundedRect(cr, badgeX, badgeY, badgeW, badgeH, badgeH / 2.0);
            cairo_set_source_rgba(cr, 0.05, 0.55, 0.95, 0.28);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.20, 0.70, 1.0, 0.60);
            cairo_set_line_width(cr, 0.8 * zoom);
            cairo_stroke(cr);

            cairo_set_source_rgb(cr, 0.75, 0.90, 1.0);
            cairo_move_to(cr, badgeX + (badgeW - cntExt.width) / 2.0,
                          badgeY + (badgeH + cntExt.height) / 2.0 - 0.5 * zoom);
            cairo_show_text(cr, countStr.c_str());
        }
    }

    cairo_restore(cr);

    // Workspace Canvas Find search match aura (TASK-4.3)
    drawSearchAura(cr, state, node, sx, sy, sw, sh, radius);
}

void WorkspaceRenderer::drawMagneticSnapGuides(cairo_t* cr, const WorkspaceState& state) {
    if (state.dragSnap.activeSnapGuideLines.empty())
        return;

    cairo_save(cr);
    for (const auto& g : state.dragSnap.activeSnapGuideLines) {
        const FluidCore::Point s1 = state.viewport.worldToScreen(g.start.x, g.start.y);
        const FluidCore::Point s2 = state.viewport.worldToScreen(g.end.x, g.end.y);

        cairo_set_source_rgba(cr, 0.0, 0.82, 1.0, 0.30);
        cairo_set_line_width(cr, 4.0);
        cairo_move_to(cr, s1.x, s1.y);
        cairo_line_to(cr, s2.x, s2.y);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, 0.0, 0.85, 1.0, 0.95);
        double dashes[] = {5.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_set_line_width(cr, 1.6);
        cairo_move_to(cr, s1.x, s1.y);
        cairo_line_to(cr, s2.x, s2.y);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0.0);
    }
    cairo_restore(cr);
}

void WorkspaceRenderer::drawStackMergeGhost(cairo_t* cr, const WorkspaceState& state,
                                            FluidCore::FluidCoreAPI& api) {
    if (state.dragSnap.activeSnapType != FluidCore::SnapType::StackMerge ||
        state.dragSnap.activeMergeTargetId.empty()) {
        return;
    }

    FluidCore::Rectangle targetBounds = api.getNodeBounds(state.dragSnap.activeMergeTargetId);
    if (targetBounds.w <= 0.0 || targetBounds.h <= 0.0) {
        return;
    }

    const double zoom = state.viewport.zoom;
    const FluidCore::Point sp = state.viewport.worldToScreen(targetBounds.x, targetBounds.y);
    const double sw = targetBounds.w * zoom;
    const double sh = targetBounds.h * zoom;
    const double radius = 10.0 * zoom;

    cairo_save(cr);

    drawRoundedRect(cr, sp.x - 4.0, sp.y - 4.0, sw + 8.0, sh + 8.0, radius);
    cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.18);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.05, 0.70, 1.0, 0.90);
    double dashes[] = {6.0, 4.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_set_line_width(cr, 2.5);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0.0);

    const double badgeW = std::min(130.0 * zoom, sw * 0.8);
    const double badgeH = 28.0 * zoom;
    const double badgeX = sp.x + (sw - badgeW) / 2.0;
    const double badgeY = sp.y + (sh - badgeH) / 2.0;

    drawRoundedRect(cr, badgeX, badgeY, badgeW, badgeH, badgeH / 2.0);
    cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, 0.90);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.70);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    if (badgeW > 40.0) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0 * zoom);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, "+ Drop to Stack", &ext);
        cairo_move_to(cr, badgeX + (badgeW - ext.width) / 2.0,
                      badgeY + (badgeH + ext.height) / 2.0 - 0.5 * zoom);
        cairo_show_text(cr, "+ Drop to Stack");
    }

    cairo_restore(cr);
}

void WorkspaceRenderer::drawSearchAura(cairo_t* cr, const WorkspaceState& state,
                                       const FluidCore::WorkspaceNode* node, double sx, double sy,
                                       double sw, double sh, double radius) {
    if (!state.search.active || !node) {
        return;
    }

    const std::string& id = node->id();
    const bool isDirectMatch = state.search.hasMatch(id);
    const bool isTopLevelMatch = state.search.hasTopLevelMatch(id);

    if (!isDirectMatch && !isTopLevelMatch) {
        return;
    }

    bool isActiveMatch = false;
    if (state.search.activeMatchIndex >= 0 &&
        state.search.activeMatchIndex < static_cast<int>(state.search.matches.size())) {
        const auto& activeM = state.search.matches[state.search.activeMatchIndex];
        if (activeM.nodeId == id || activeM.topLevelNodeId == id) {
            isActiveMatch = true;
        }
    }

    const double zoom = state.viewport.zoom;

    cairo_save(cr);

    if (isActiveMatch) {
        // Prominent Golden Amber Active Match Halo
        drawRoundedRect(cr, sx - 6.0, sy - 6.0, sw + 12.0, sh + 12.0, radius + 6.0);
        cairo_set_source_rgba(cr, 0.96, 0.62, 0.07, 0.35);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.96, 0.62, 0.07, 0.95);
        cairo_set_line_width(cr, std::max(2.5, 3.0 * zoom));
        cairo_stroke(cr);

        // Floating match badge [Match X/N]
        const std::string badgeStr = "Match " + std::to_string(state.search.activeMatchIndex + 1) +
                                     "/" + std::to_string(state.search.matches.size());
        const double badgeH = std::clamp(20.0 * zoom, 16.0, 24.0);
        const double badgeW = std::clamp(80.0 * zoom, 60.0, 100.0);
        const double badgeX = sx + 12.0 * zoom;
        const double badgeY = sy - badgeH / 2.0;

        drawRoundedRect(cr, badgeX, badgeY, badgeW, badgeH, badgeH / 2.0);
        cairo_set_source_rgba(cr, 0.96, 0.62, 0.07, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, std::clamp(9.5 * zoom, 8.0, 12.0));
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, badgeStr.c_str(), &ext);
        cairo_move_to(cr, badgeX + (badgeW - ext.width) / 2.0,
                      badgeY + (badgeH + ext.height) / 2.0 - 0.5);
        cairo_show_text(cr, badgeStr.c_str());
    } else {
        // Soft Glowing Amber Match Outline
        drawRoundedRect(cr, sx - 4.0, sy - 4.0, sw + 8.0, sh + 8.0, radius + 4.0);
        cairo_set_source_rgba(cr, 0.96, 0.62, 0.07, 0.18);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.96, 0.62, 0.07, 0.75);
        cairo_set_line_width(cr, std::max(1.8, 2.0 * zoom));
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

void WorkspaceRenderer::draw(cairo_t* cr, const WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                             ExcerptTileCache* excerptTileCache, int width, int height) {
    cairo_set_source_rgb(cr, 0.975, 0.982, 0.990);
    cairo_paint(cr);

    drawBackgroundGrid(cr, state, width, height);
    drawGraphEdges(cr, state, api);

    const double zoom = state.viewport.zoom;
    const double originX = state.viewport.originX;
    const double originY = state.viewport.originY;

    const double pad = 150.0 / zoom;
    const FluidCore::Rectangle viewport{originX - pad, originY - pad, width / zoom + 2.0 * pad,
                                        height / zoom + 2.0 * pad};
    const std::vector<FluidCore::WorkspaceNode*> visibleNodes = api.queryVisibleNodes(viewport);

    for (const FluidCore::WorkspaceNode* node : visibleNodes) {
        if (dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - originX) * zoom;
            const double sy = (b.y - originY) * zoom;
            const double sw = b.w * zoom;
            const double sh = b.h * zoom;
            drawCardStack(cr, state, node, excerptTileCache, sx, sy, sw, sh);
        } else if (dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - originX) * zoom;
            const double sy = (b.y - originY) * zoom;
            const double sw = b.w * zoom;
            const double sh = b.h * zoom;
            drawExcerptCard(cr, state, node, excerptTileCache, sx, sy, sw, sh);
        } else if (const auto* strokeNode =
                       dynamic_cast<const FluidCore::CanvasStrokeNode*>(node)) {
            const auto& stroke = strokeNode->stroke();
            if (stroke.points.empty())
                continue;

            const bool isHoveredForErase =
                !state.inking.hoveredEraserStrokeIds.empty() &&
                (std::find(state.inking.hoveredEraserStrokeIds.begin(),
                           state.inking.hoveredEraserStrokeIds.end(),
                           strokeNode->id()) != state.inking.hoveredEraserStrokeIds.end());

            const auto& pt0 = stroke.points[0];
            const double baseW = stroke.width * zoom;

            if (isHoveredForErase) {
                cairo_save(cr);
                cairo_set_source_rgba(cr, 1.0, 0.22, 0.22, 0.45);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                const double glowWidth = (stroke.width + 10.0) * zoom;
                if (stroke.points.size() == 1) {
                    cairo_new_path(cr);
                    cairo_arc(cr, (pt0.x - originX) * zoom, (pt0.y - originY) * zoom,
                              std::max(2.0, glowWidth / 2.0), 0, 2 * M_PI);
                    cairo_fill(cr);
                } else {
                    cairo_set_line_width(cr, std::max(2.0, glowWidth));
                    cairo_new_path(cr);
                    cairo_move_to(cr, (pt0.x - originX) * zoom, (pt0.y - originY) * zoom);
                    for (size_t i = 1; i < stroke.points.size(); ++i) {
                        const auto& pt = stroke.points[i];
                        cairo_line_to(cr, (pt.x - originX) * zoom, (pt.y - originY) * zoom);
                    }
                    cairo_stroke(cr);
                }
                cairo_restore(cr);
            }

            cairo_set_source_rgba(
                cr, ((stroke.color >> 16) & 0xFF) / 255.0, ((stroke.color >> 8) & 0xFF) / 255.0,
                (stroke.color & 0xFF) / 255.0, stroke.tool == "highlighter" ? 0.45 : 1.0);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

            if (stroke.points.size() == 1) {
                const double w0 = (stroke.pressures.empty() || stroke.pressures.size() < 1)
                                      ? baseW
                                      : baseW * (0.25 + 0.75 * stroke.pressures[0]);
                cairo_new_path(cr);
                cairo_arc(cr, (pt0.x - originX) * zoom, (pt0.y - originY) * zoom,
                          std::max(1.0, w0 / 2.0), 0, 2 * M_PI);
                cairo_fill(cr);
            } else if (stroke.pressures.empty() ||
                       stroke.pressures.size() != stroke.points.size()) {
                // Constant base_width mode
                cairo_set_line_width(cr, std::max(0.5, baseW));
                cairo_new_path(cr);
                cairo_move_to(cr, (pt0.x - originX) * zoom, (pt0.y - originY) * zoom);
                for (size_t i = 1; i < stroke.points.size(); ++i) {
                    const auto& pt = stroke.points[i];
                    cairo_line_to(cr, (pt.x - originX) * zoom, (pt.y - originY) * zoom);
                }
                cairo_stroke(cr);
            } else {
                // Variable pressure mode: w_i = base_width * (0.25 + 0.75 * p_i)
                for (size_t i = 1; i < stroke.points.size(); ++i) {
                    const auto& pPrev = stroke.points[i - 1];
                    const auto& pCurr = stroke.points[i];
                    const double segPressure =
                        (stroke.pressures[i - 1] + stroke.pressures[i]) / 2.0;
                    const double segWidth = baseW * (0.25 + 0.75 * segPressure);
                    cairo_set_line_width(cr, std::max(0.5, segWidth));
                    cairo_new_path(cr);
                    cairo_move_to(cr, (pPrev.x - originX) * zoom, (pPrev.y - originY) * zoom);
                    cairo_line_to(cr, (pCurr.x - originX) * zoom, (pCurr.y - originY) * zoom);
                    cairo_stroke(cr);
                }
            }
        } else {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - originX) * zoom;
            const double sy = (b.y - originY) * zoom;
            const double sw = b.w * zoom;
            const double sh = b.h * zoom;
            drawGenericNode(cr, state, node, sx, sy, sw, sh);
        }
    }

    // Render active wet ink with pressure dynamics
    if (state.inking.isDrawing &&
        (state.inking.currentTool == "pen" || state.inking.currentTool == "highlighter")) {
        cairo_set_source_rgba(cr, ((state.inking.currentColor >> 16) & 0xFF) / 255.0,
                              ((state.inking.currentColor >> 8) & 0xFF) / 255.0,
                              (state.inking.currentColor & 0xFF) / 255.0,
                              state.inking.currentTool == "highlighter" ? 0.45 : 1.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

        const auto& samples = state.inking.stabilizer.rawSamples();
        const double baseW = state.inking.currentWidth * zoom;

        if (samples.size() == 1) {
            const double w0 = baseW * (0.25 + 0.75 * samples[0].pressure);
            cairo_new_path(cr);
            cairo_arc(cr, (samples[0].point.x - originX) * zoom,
                      (samples[0].point.y - originY) * zoom, std::max(1.0, w0 / 2.0), 0, 2 * M_PI);
            cairo_fill(cr);
        } else if (samples.size() > 1) {
            for (size_t i = 1; i < samples.size(); ++i) {
                const double segPressure = (samples[i - 1].pressure + samples[i].pressure) / 2.0;
                const double segWidth = baseW * (0.25 + 0.75 * segPressure);
                cairo_set_line_width(cr, std::max(0.5, segWidth));
                cairo_new_path(cr);
                cairo_move_to(cr, (samples[i - 1].point.x - originX) * zoom,
                              (samples[i - 1].point.y - originY) * zoom);
                cairo_line_to(cr, (samples[i].point.x - originX) * zoom,
                              (samples[i].point.y - originY) * zoom);
                cairo_stroke(cr);
            }
        }
    }

    drawMagneticSnapGuides(cr, state);
    drawStackMergeGhost(cr, state, api);

    if (state.isDropHovering) {
        const double ghostW = 260.0 * zoom;
        const double ghostH = 140.0 * zoom;
        const double gx = state.dropHoverScreenX;
        const double gy = state.dropHoverScreenY;

        cairo_save(cr);
        drawRoundedRect(cr, gx, gy, ghostW, ghostH, 8.0 * std::min(1.0, zoom));
        cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, 0.12);
        cairo_fill_preserve(cr);

        cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, 0.85);
        double dashes[] = {6.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_set_line_width(cr, 1.8);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Eraser cursor radius indicator & target feedback
    if (state.inking.currentTool == "eraser" && state.isEraserPointerHovering) {
        cairo_save(cr);
        const double cx = state.lastMouseX;
        const double cy = state.lastMouseY;
        const double r = state.inking.eraserRadius;

        const bool hasTargets =
            state.inking.isDrawing || !state.inking.hoveredEraserStrokeIds.empty();

        if (hasTargets) {
            // Targeted: active solid deletion indicator ring
            cairo_set_source_rgba(cr, 1.0, 0.20, 0.20, 0.12);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            cairo_fill_preserve(cr);

            cairo_set_source_rgba(cr, 1.0, 0.25, 0.25, 0.85);
            cairo_set_line_width(cr, 1.8);
            cairo_stroke(cr);
        } else {
            // Idle: subtle dashed/faint ring
            cairo_set_source_rgba(cr, 0.90, 0.35, 0.35, 0.06);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            cairo_fill_preserve(cr);

            cairo_set_source_rgba(cr, 0.85, 0.30, 0.30, 0.45);
            double dashes[] = {3.0, 3.0};
            cairo_set_dash(cr, dashes, 2, 0.0);
            cairo_set_line_width(cr, 1.2);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0.0);
        }
        cairo_restore(cr);
    }

    drawMinimap(cr, state, api, width, height);
}

} // namespace FluidCoreApp
