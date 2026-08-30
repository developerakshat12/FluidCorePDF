#include "WorkspaceView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace FluidCoreApp {
namespace {

constexpr double kMinZoom = 0.05; // 5%
constexpr double kMaxZoom = 10.0; // 1000%
constexpr double kBaseGridStep = 32.0;
constexpr double kMajorGridMultiple = 5.0; // Major accents every 5 dots (160 pt)

void drawRoundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
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

} // namespace

WorkspaceView::WorkspaceView(FluidCore::FluidCoreAPI& api) : m_api(api) {
    m_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(m_area, TRUE);
    gtk_widget_add_events(m_area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                      GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
                                      GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
                                      GDK_SMOOTH_SCROLL_MASK);

    g_signal_connect(m_area, "draw", G_CALLBACK(WorkspaceView::drawCallback), this);
    g_signal_connect(m_area, "scroll-event", G_CALLBACK(WorkspaceView::scrollCallback), this);
    g_signal_connect(m_area, "button-press-event", G_CALLBACK(WorkspaceView::buttonPressCallback),
                     this);
    g_signal_connect(m_area, "button-release-event",
                     G_CALLBACK(WorkspaceView::buttonReleaseCallback), this);
    g_signal_connect(m_area, "motion-notify-event", G_CALLBACK(WorkspaceView::motionCallback),
                     this);
    g_signal_connect(m_area, "key-press-event", G_CALLBACK(WorkspaceView::keyPressCallback), this);
}

WorkspaceView::~WorkspaceView() = default;

FluidCore::Point WorkspaceView::screenToWorld(double screenX, double screenY) const {
    return {m_originX + screenX / m_zoom, m_originY + screenY / m_zoom};
}

FluidCore::Point WorkspaceView::worldToScreen(double worldX, double worldY) const {
    return {(worldX - m_originX) * m_zoom, (worldY - m_originY) * m_zoom};
}

void WorkspaceView::zoomAt(double factor, double focalScreenX, double focalScreenY) {
    const double oldZoom = m_zoom;
    const double newZoom = std::clamp(oldZoom * factor, kMinZoom, kMaxZoom);
    if (std::abs(newZoom - oldZoom) < 1e-6)
        return;

    // Preserve world coordinate under focal point:
    // focalWorld = origin + focalScreen / zoom
    const double focalWorldX = m_originX + focalScreenX / oldZoom;
    const double focalWorldY = m_originY + focalScreenY / oldZoom;

    m_zoom = newZoom;
    m_originX = focalWorldX - focalScreenX / newZoom;
    m_originY = focalWorldY - focalScreenY / newZoom;

    gtk_widget_queue_draw(m_area);
}

void WorkspaceView::setZoom(double zoom) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double cx = alloc.width > 0 ? alloc.width / 2.0 : 0.0;
    const double cy = alloc.height > 0 ? alloc.height / 2.0 : 0.0;
    zoomAt(zoom / m_zoom, cx, cy);
}

void WorkspaceView::panBy(double dxScreen, double dyScreen) {
    m_originX -= dxScreen / m_zoom;
    m_originY -= dyScreen / m_zoom;
    gtk_widget_queue_draw(m_area);
}

void WorkspaceView::centerOn(double worldX, double worldY) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double vw = alloc.width > 0 ? alloc.width / m_zoom : 800.0;
    const double vh = alloc.height > 0 ? alloc.height / m_zoom : 600.0;

    m_originX = worldX - vw / 2.0;
    m_originY = worldY - vh / 2.0;
    gtk_widget_queue_draw(m_area);
}

void WorkspaceView::resetView() {
    const FluidCore::Rectangle bounds = m_api.getWorkspaceBounds();
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double viewW = alloc.width > 0 ? static_cast<double>(alloc.width) : 800.0;
    const double viewH = alloc.height > 0 ? static_cast<double>(alloc.height) : 600.0;

    if (bounds.w > 0.0 && bounds.h > 0.0) {
        // Fit all nodes with a 15% margin
        const double margin = 40.0;
        const double fitZoomX = viewW / (bounds.w + margin * 2.0);
        const double fitZoomY = viewH / (bounds.h + margin * 2.0);
        m_zoom = std::clamp(std::min(fitZoomX, fitZoomY), 0.2, 1.5);
        const double centerWorldX = bounds.x + bounds.w / 2.0;
        const double centerWorldY = bounds.y + bounds.h / 2.0;
        m_originX = centerWorldX - (viewW / m_zoom) / 2.0;
        m_originY = centerWorldY - (viewH / m_zoom) / 2.0;
    } else {
        m_zoom = 1.0;
        m_originX = 0.0;
        m_originY = 0.0;
    }
    gtk_widget_queue_draw(m_area);
}

void WorkspaceView::setMinimapVisible(bool visible) {
    if (m_showMinimap != visible) {
        m_showMinimap = visible;
        gtk_widget_queue_draw(m_area);
    }
}

FluidCore::Rectangle WorkspaceView::getMinimapRect(int viewWidth, int viewHeight) const {
    const double w = std::min(m_minimapWidth, viewWidth * 0.4);
    const double h = std::min(m_minimapHeight, viewHeight * 0.4);
    const double x = viewWidth - w - m_minimapMargin;
    const double y = viewHeight - h - m_minimapMargin;
    return {x, y, w, h};
}

bool WorkspaceView::minimapHitTest(double screenX, double screenY, int viewWidth,
                                   int viewHeight) const {
    if (!m_showMinimap)
        return false;
    const FluidCore::Rectangle r = getMinimapRect(viewWidth, viewHeight);
    return screenX >= r.x && screenX <= r.x + r.w && screenY >= r.y && screenY <= r.y + r.h;
}

void WorkspaceView::handleMinimapInteraction(double screenX, double screenY, int viewWidth,
                                             int viewHeight) {
    const FluidCore::Rectangle mm = getMinimapRect(viewWidth, viewHeight);
    if (mm.w <= 0.0 || mm.h <= 0.0)
        return;

    // Determine current global bounding box combining nodes and viewport
    FluidCore::Rectangle wsBounds = m_api.getWorkspaceBounds();
    const double currentViewW = viewWidth / m_zoom;
    const double currentViewH = viewHeight / m_zoom;

    if (wsBounds.w <= 0.0 || wsBounds.h <= 0.0) {
        wsBounds = {m_originX, m_originY, currentViewW, currentViewH};
    } else {
        const double minX = std::min(wsBounds.x, m_originX);
        const double minY = std::min(wsBounds.y, m_originY);
        const double maxX = std::max(wsBounds.x + wsBounds.w, m_originX + currentViewW);
        const double maxY = std::max(wsBounds.y + wsBounds.h, m_originY + currentViewH);
        wsBounds = {minX, minY, maxX - minX, maxY - minY};
    }

    // Add 10% padding around bounding box in minimap space
    const double padX = wsBounds.w * 0.1;
    const double padY = wsBounds.h * 0.1;
    const double mapWorldX = wsBounds.x - padX;
    const double mapWorldY = wsBounds.y - padY;
    const double mapWorldW = wsBounds.w + padX * 2.0;
    const double mapWorldH = wsBounds.h + padY * 2.0;

    const double normX = std::clamp((screenX - mm.x) / mm.w, 0.0, 1.0);
    const double normY = std::clamp((screenY - mm.y) / mm.h, 0.0, 1.0);

    const double targetWorldX = mapWorldX + normX * mapWorldW;
    const double targetWorldY = mapWorldY + normY * mapWorldH;

    centerOn(targetWorldX, targetWorldY);
}

void WorkspaceView::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    auto* self = static_cast<WorkspaceView*>(userData);
    GtkAllocation rect;
    gtk_widget_get_allocation(self->m_area, &rect);
    self->draw(cr, rect.width, rect.height);
}

gboolean WorkspaceView::scrollCallback(GtkWidget*, GdkEventScroll* event, gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onScroll(event);
}

gboolean WorkspaceView::buttonPressCallback(GtkWidget*, GdkEventButton* event, gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onButtonPress(event);
}

gboolean WorkspaceView::buttonReleaseCallback(GtkWidget*, GdkEventButton* event,
                                              gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onButtonRelease(event);
}

gboolean WorkspaceView::motionCallback(GtkWidget*, GdkEventMotion* event, gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onMotion(event);
}

gboolean WorkspaceView::keyPressCallback(GtkWidget*, GdkEventKey* event, gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onKeyPress(event);
}

gboolean WorkspaceView::onScroll(GdkEventScroll* event) {
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;

    if (ctrl) {
        // Focal zoom centered on cursor
        double factor = 1.0;
        if (event->direction == GDK_SCROLL_UP) {
            factor = 1.15;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            factor = 0.87;
        } else if (event->direction == GDK_SCROLL_SMOOTH) {
            double dx = 0.0, dy = 0.0;
            gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &dx, &dy);
            factor = std::pow(0.95, dy);
        }
        zoomAt(factor, event->x, event->y);
        return TRUE;
    }

    // Panning via scroll wheel or 2-finger touchpad scroll
    double dx = 0.0, dy = 0.0;
    if (event->direction == GDK_SCROLL_UP) {
        dy = -40.0;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        dy = 40.0;
    } else if (event->direction == GDK_SCROLL_LEFT) {
        dx = -40.0;
    } else if (event->direction == GDK_SCROLL_RIGHT) {
        dx = 40.0;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        double sdx = 0.0, sdy = 0.0;
        gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &sdx, &sdy);
        dx = sdx * 30.0;
        dy = sdy * 30.0;
    }

    if (dx != 0.0 || dy != 0.0) {
        panBy(-dx, -dy);
        return TRUE;
    }
    return FALSE;
}

gboolean WorkspaceView::onButtonPress(GdkEventButton* event) {
    gtk_widget_grab_focus(m_area);

    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);

    if (event->button == GDK_BUTTON_PRIMARY &&
        minimapHitTest(event->x, event->y, alloc.width, alloc.height)) {
        m_isMinimapDragging = true;
        handleMinimapInteraction(event->x, event->y, alloc.width, alloc.height);
        return TRUE;
    }

    // Middle click, right click, or left click initiates canvas pan
    if (event->button == GDK_BUTTON_PRIMARY || event->button == GDK_BUTTON_MIDDLE ||
        event->button == GDK_BUTTON_SECONDARY) {
        m_isPanning = true;
        m_lastMouseX = event->x;
        m_lastMouseY = event->y;

        // Set grabbing hand cursor
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            GdkDisplay* display = gdk_window_get_display(win);
            GdkCursor* cursor = gdk_cursor_new_from_name(display, "grabbing");
            if (!cursor) {
                cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
            }
            gdk_window_set_cursor(win, cursor);
            if (cursor)
                g_object_unref(cursor);
        }
        return TRUE;
    }

    return FALSE;
}

gboolean WorkspaceView::onButtonRelease(GdkEventButton* event) {
    (void)event;
    if (m_isMinimapDragging) {
        m_isMinimapDragging = false;
        return TRUE;
    }

    if (m_isPanning) {
        m_isPanning = false;
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            gdk_window_set_cursor(win, nullptr); // reset cursor
        }
        return TRUE;
    }
    return FALSE;
}

gboolean WorkspaceView::onMotion(GdkEventMotion* event) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);

    if (m_isMinimapDragging) {
        handleMinimapInteraction(event->x, event->y, alloc.width, alloc.height);
        return TRUE;
    }

    if (m_isPanning) {
        const double dx = event->x - m_lastMouseX;
        const double dy = event->y - m_lastMouseY;
        m_lastMouseX = event->x;
        m_lastMouseY = event->y;
        panBy(dx, dy);
        return TRUE;
    }

    // Hover cursor feedback over minimap
    GdkWindow* win = gtk_widget_get_window(m_area);
    if (win) {
        GdkDisplay* display = gdk_window_get_display(win);
        if (minimapHitTest(event->x, event->y, alloc.width, alloc.height)) {
            GdkCursor* pointerCursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(win, pointerCursor);
            if (pointerCursor)
                g_object_unref(pointerCursor);
        } else {
            gdk_window_set_cursor(win, nullptr);
        }
    }

    return FALSE;
}

gboolean WorkspaceView::onKeyPress(GdkEventKey* event) {
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
    const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;

    if (ctrl) {
        if (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal ||
            event->keyval == GDK_KEY_KP_Add) {
            zoomAt(1.2, cx, cy);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_minus || event->keyval == GDK_KEY_KP_Subtract) {
            zoomAt(0.8333, cx, cy);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_0 || event->keyval == GDK_KEY_KP_0) {
            resetView();
            return TRUE;
        }
    }

    // Pan via arrow keys
    if (event->keyval == GDK_KEY_Left) {
        panBy(40.0, 0.0);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Right) {
        panBy(-40.0, 0.0);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Up) {
        panBy(0.0, 40.0);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down) {
        panBy(0.0, -40.0);
        return TRUE;
    }

    return FALSE;
}

void WorkspaceView::drawBackgroundGrid(cairo_t* cr, int width, int height) {
    const double step = kBaseGridStep * m_zoom;
    if (step < 8.0) {
        // When zoomed far out, draw major grid lines only
        const double majorStep = step * kMajorGridMultiple;
        if (majorStep >= 16.0) {
            cairo_set_source_rgba(cr, 0.85, 0.88, 0.92, 0.6);
            cairo_set_line_width(cr, 1.0);
            double startX = std::fmod(-m_originX * m_zoom, majorStep);
            if (startX < 0)
                startX += majorStep;
            for (double x = startX - majorStep; x < width; x += majorStep) {
                cairo_move_to(cr, x, 0.0);
                cairo_line_to(cr, x, height);
            }
            double startY = std::fmod(-m_originY * m_zoom, majorStep);
            if (startY < 0)
                startY += majorStep;
            for (double y = startY - majorStep; y < height; y += majorStep) {
                cairo_move_to(cr, 0.0, y);
                cairo_line_to(cr, width, y);
            }
            cairo_stroke(cr);
        }
        return;
    }

    // Infinite dot grid with major accents
    double startX = std::fmod(-m_originX * m_zoom, step);
    if (startX < 0)
        startX += step;
    double startY = std::fmod(-m_originY * m_zoom, step);
    if (startY < 0)
        startY += step;

    // Minor dots
    cairo_set_source_rgb(cr, 0.82, 0.85, 0.90);
    const double dotRadius = std::clamp(1.0 * std::sqrt(m_zoom), 0.8, 1.6);
    for (double x = startX - step; x < width; x += step) {
        for (double y = startY - step; y < height; y += step) {
            cairo_arc(cr, x, y, dotRadius, 0.0, 2.0 * M_PI);
        }
    }
    cairo_fill(cr);

    // Major cross accents at 5-dot intervals
    const double majorStep = step * kMajorGridMultiple;
    if (majorStep >= 30.0) {
        double mStartX = std::fmod(-m_originX * m_zoom, majorStep);
        if (mStartX < 0)
            mStartX += majorStep;
        double mStartY = std::fmod(-m_originY * m_zoom, majorStep);
        if (mStartY < 0)
            mStartY += majorStep;

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

void WorkspaceView::drawMinimap(cairo_t* cr, int width, int height) {
    if (!m_showMinimap)
        return;

    const FluidCore::Rectangle mm = getMinimapRect(width, height);
    if (mm.w <= 40.0 || mm.h <= 30.0)
        return;

    // Background card container with drop shadow & frosted styling
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

    // Compute global bounding box of workspace including visible viewport
    FluidCore::Rectangle wsBounds = m_api.getWorkspaceBounds();
    const double currentViewW = width / m_zoom;
    const double currentViewH = height / m_zoom;

    if (wsBounds.w <= 0.0 || wsBounds.h <= 0.0) {
        wsBounds = {m_originX, m_originY, currentViewW, currentViewH};
    } else {
        const double minX = std::min(wsBounds.x, m_originX);
        const double minY = std::min(wsBounds.y, m_originY);
        const double maxX = std::max(wsBounds.x + wsBounds.w, m_originX + currentViewW);
        const double maxY = std::max(wsBounds.y + wsBounds.h, m_originY + currentViewH);
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

    // Clip content to minimap inner body
    cairo_save(cr);
    drawRoundedRect(cr, mm.x + 1.0, mm.y + 1.0, mm.w - 2.0, mm.h - 2.0, 7.0);
    cairo_clip(cr);

    // Draw miniature nodes
    const FluidCore::Rectangle queryAll{mapWorldX, mapWorldY, mapWorldW, mapWorldH};
    cairo_set_source_rgba(cr, 0.45, 0.58, 0.75, 0.7);
    for (const FluidCore::WorkspaceNode* node : m_api.queryVisibleNodes(queryAll)) {
        const FluidCore::Rectangle b = node->bounds();
        const FluidCore::Point p1 = worldToMinimap(b.x, b.y);
        const FluidCore::Point p2 = worldToMinimap(b.x + b.w, b.y + b.h);
        const double mw = std::max(p2.x - p1.x, 3.0);
        const double mh = std::max(p2.y - p1.y, 3.0);

        drawRoundedRect(cr, p1.x, p1.y, mw, mh, 2.0);
        cairo_fill(cr);
    }

    // Draw active viewport frame indicator
    const FluidCore::Point vp1 = worldToMinimap(m_originX, m_originY);
    const FluidCore::Point vp2 = worldToMinimap(m_originX + currentViewW, m_originY + currentViewH);
    const double vpw = std::max(vp2.x - vp1.x, 6.0);
    const double vph = std::max(vp2.y - vp1.y, 6.0);

    // Glowing cyan viewport highlight
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

    // Zoom percentage text on top right
    std::ostringstream oss;
    oss << static_cast<int>(std::round(m_zoom * 100.0)) << "%";
    const std::string zoomStr = oss.str();
    cairo_text_extents_t ext;
    cairo_text_extents(cr, zoomStr.c_str(), &ext);
    cairo_move_to(cr, mm.x + mm.w - ext.width - 8.0, mm.y + 14.0);
    cairo_show_text(cr, zoomStr.c_str());

    cairo_restore(cr);
}

void WorkspaceView::draw(cairo_t* cr, int width, int height) {
    // Clear background canvas with modern slate-50 tone
    cairo_set_source_rgb(cr, 0.975, 0.982, 0.990);
    cairo_paint(cr);

    // Render infinite dot-grid
    drawBackgroundGrid(cr, width, height);

    // Viewport spatial culling query (O(log N) R-tree query)
    const FluidCore::Rectangle viewport{m_originX, m_originY, width / m_zoom, height / m_zoom};
    const std::vector<FluidCore::WorkspaceNode*> visibleNodes = m_api.queryVisibleNodes(viewport);

    for (const FluidCore::WorkspaceNode* node : visibleNodes) {
        const FluidCore::Rectangle b = node->bounds();
        const double sx = (b.x - m_originX) * m_zoom;
        const double sy = (b.y - m_originY) * m_zoom;
        const double sw = b.w * m_zoom;
        const double sh = b.h * m_zoom;

        // Soft elevation card shadow
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
        drawRoundedRect(cr, sx + 2.0, sy + 4.0, sw, sh, 8.0 * std::min(1.0, m_zoom));
        cairo_fill(cr);

        // Card container
        drawRoundedRect(cr, sx, sy, sw, sh, 8.0 * std::min(1.0, m_zoom));
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_fill_preserve(cr);

        // Border
        cairo_set_source_rgb(cr, 0.75, 0.82, 0.90);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);

        // Accent top bar chip
        cairo_save(cr);
        drawRoundedRect(cr, sx, sy, sw, sh, 8.0 * std::min(1.0, m_zoom));
        cairo_clip(cr);

        const double headerH = std::min(24.0 * m_zoom, sh * 0.35);
        cairo_rectangle(cr, sx, sy, sw, headerH);
        cairo_set_source_rgb(cr, 0.92, 0.95, 0.98);
        cairo_fill(cr);

        // Card header border line
        cairo_move_to(cr, sx, sy + headerH);
        cairo_line_to(cr, sx + sw, sy + headerH);
        cairo_set_source_rgb(cr, 0.82, 0.88, 0.94);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        // Card ID label
        if (m_zoom >= 0.25) {
            cairo_set_source_rgb(cr, 0.20, 0.28, 0.38);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, std::clamp(11.0 * m_zoom, 9.0, 14.0));
            cairo_move_to(cr, sx + 8.0 * m_zoom, sy + headerH * 0.7);
            cairo_show_text(cr, node->id().c_str());
        }

        cairo_restore(cr);
    }

    // Render floating minimap HUD
    drawMinimap(cr, width, height);
}

} // namespace FluidCoreApp
