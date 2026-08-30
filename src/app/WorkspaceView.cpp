#include "WorkspaceView.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/ExcerptPayload.h"
#include "undo/WorkspaceCommands.h"


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

    // Setup GTK Drag and Drop Destination (specs/integration.md §2)
    static const GtkTargetEntry dropTargets[] = {
        {const_cast<gchar*>("application/x-fluid-excerpt"), GTK_TARGET_SAME_APP, 0},
        {const_cast<gchar*>("text/plain"), 0, 1}};
    gtk_drag_dest_set(m_area, GTK_DEST_DEFAULT_ALL, dropTargets, G_N_ELEMENTS(dropTargets),
                      GDK_ACTION_COPY);

    g_signal_connect(m_area, "drag-data-received",
                     G_CALLBACK(WorkspaceView::dragDataReceivedCallback), this);
    g_signal_connect(m_area, "drag-motion", G_CALLBACK(WorkspaceView::dragMotionCallback), this);
    g_signal_connect(m_area, "drag-leave", G_CALLBACK(WorkspaceView::dragLeaveCallback), this);
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

void WorkspaceView::setTool(const std::string& tool) {
    if (m_currentTool != tool) {
        m_currentTool = tool;
        if (tool == "highlighter") {
            m_currentColor = 0xFFFF00; // Bright yellow highlighter
            m_currentWidth = 14.0;
        } else if (tool == "pen") {
            m_currentColor = 0x000000; // Black pen
            m_currentWidth = 2.0;
        } else if (tool == "eraser") {
            m_currentWidth = 24.0;
        }
        m_isDrawing = false;
        m_hasWetSegment = false;

        if (m_area) {
            GdkWindow* win = gtk_widget_get_window(m_area);
            if (win) {
                GdkDisplay* display = gdk_window_get_display(win);
                GdkCursor* cursor = nullptr;
                if (tool == "eraser") {
                    cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
                } else if (tool == "pen" || tool == "highlighter") {
                    cursor = gdk_cursor_new_for_display(display, GDK_PENCIL);
                    if (!cursor) {
                        cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
                    }
                }
                gdk_window_set_cursor(win, cursor);
                if (cursor) {
                    g_object_unref(cursor);
                }
            }
        }
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

    const double padX = std::max(wsBounds.w * 0.1, 100.0);
    const double padY = std::max(wsBounds.h * 0.1, 100.0);
    const double mapWorldX = wsBounds.x - padX;
    const double mapWorldY = wsBounds.y - padY;
    const double mapWorldW = wsBounds.w + padX * 2.0;
    const double mapWorldH = wsBounds.h + padY * 2.0;

    const double nx = std::clamp((screenX - (mm.x + 8.0)) / (mm.w - 16.0), 0.0, 1.0);
    const double ny = std::clamp((screenY - (mm.y + 24.0)) / (mm.h - 32.0), 0.0, 1.0);

    const double targetCenterX = mapWorldX + nx * mapWorldW;
    const double targetCenterY = mapWorldY + ny * mapWorldH;

    centerOn(targetCenterX, targetCenterY);
}

void WorkspaceView::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    auto* self = static_cast<WorkspaceView*>(userData);
    GtkAllocation alloc;
    gtk_widget_get_allocation(self->m_area, &alloc);
    self->draw(cr, alloc.width, alloc.height);
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

void WorkspaceView::dragDataReceivedCallback(GtkWidget*, GdkDragContext* context, gint x, gint y,
                                             GtkSelectionData* data, guint info, guint time,
                                             gpointer userData) {
    static_cast<WorkspaceView*>(userData)->onDragDataReceived(context, x, y, data, info, time);
}

gboolean WorkspaceView::dragMotionCallback(GtkWidget*, GdkDragContext* context, gint x, gint y,
                                           guint time, gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onDragMotion(context, x, y, time);
}

void WorkspaceView::dragLeaveCallback(GtkWidget*, GdkDragContext* context, guint time,
                                      gpointer userData) {
    static_cast<WorkspaceView*>(userData)->onDragLeave(context, time);
}

gboolean WorkspaceView::onDragMotion(GdkDragContext* context, gint x, gint y, guint time) {
    m_isDropHovering = true;
    m_dropHoverScreenX = x;
    m_dropHoverScreenY = y;
    gdk_drag_status(context, GDK_ACTION_COPY, time);
    gtk_widget_queue_draw(m_area);
    return TRUE;
}

void WorkspaceView::onDragLeave(GdkDragContext*, guint) {
    if (m_isDropHovering) {
        m_isDropHovering = false;
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::onDragDataReceived(GdkDragContext* context, gint x, gint y,
                                       GtkSelectionData* data, guint info, guint time) {
    (void)info;
    m_isDropHovering = false;

    const guchar* rawData = gtk_selection_data_get_data(data);
    const gint len = gtk_selection_data_get_length(data);
    if (!rawData || len <= 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    FluidCore::Point dropWorld = screenToWorld(x, y);

    std::optional<FluidCore::ExcerptDropPayload> payloadOpt =
        FluidCore::deserializeExcerptPayload(reinterpret_cast<const uint8_t*>(rawData), len);

    if (!payloadOpt.has_value()) {
        guchar* textData = gtk_selection_data_get_text(data);
        if (textData) {
            FluidCore::ExcerptDropPayload fallbackPayload;
            fallbackPayload.sourceDocId = "clipboard";
            fallbackPayload.sourcePageNo = 0;
            fallbackPayload.sourceNormalizedRect = {0.0, 0.0, 1.0, 1.0};
            fallbackPayload.textSnippet = reinterpret_cast<char*>(textData);
            fallbackPayload.isImageExcerpt = false;
            fallbackPayload.color = {255, 220, 0, 255};
            payloadOpt = fallbackPayload;
            g_free(textData);
        }
    }

    if (payloadOpt.has_value()) {
        const auto& payload = *payloadOpt;
        static std::size_t s_excerptCounter = 1;
        std::string cardId = "excerpt-" + std::to_string(s_excerptCounter++);

        double cardW = 260.0;
        double cardH = 140.0;

        if (payload.isImageExcerpt) {
            cardW = 280.0;
            cardH = 180.0;
        } else {
            if (payload.textSnippet.size() > 250) {
                cardH = 220.0;
            } else if (payload.textSnippet.size() > 120) {
                cardH = 170.0;
            }
        }

        FluidCore::Rectangle cardBounds{dropWorld.x, dropWorld.y, cardW, cardH};
        uint64_t timestamp = static_cast<uint64_t>(time);

        auto card = std::make_unique<FluidCore::ExcerptCardNode>(
            cardId, cardBounds, payload.sourceDocId, payload.sourcePageNo,
            payload.sourceNormalizedRect, payload.textSnippet, payload.isImageExcerpt,
            payload.color, timestamp);

        m_api.insertNode(std::move(card));
        gtk_widget_queue_draw(m_area);
        gtk_drag_finish(context, TRUE, FALSE, time);
        return;
    }

    gtk_drag_finish(context, FALSE, FALSE, time);
}

gboolean WorkspaceView::onScroll(GdkEventScroll* event) {
    const double focalX = event->x;
    const double focalY = event->y;

    if (event->state & GDK_CONTROL_MASK) {
        double delta = 0.0;
        if (event->direction == GDK_SCROLL_UP) {
            delta = -1.0;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            delta = 1.0;
        } else if (event->direction == GDK_SCROLL_SMOOTH) {
            gdouble dx = 0.0, dy = 0.0;
            gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &dx, &dy);
            delta = dy;
        }

        const double zoomMultiplier = std::pow(0.90, delta);
        zoomAt(zoomMultiplier, focalX, focalY);
        return TRUE;
    }

    double dx = 0.0;
    double dy = 0.0;
    if (event->direction == GDK_SCROLL_UP) {
        dy = 40.0;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        dy = -40.0;
    } else if (event->direction == GDK_SCROLL_LEFT) {
        dx = 40.0;
    } else if (event->direction == GDK_SCROLL_RIGHT) {
        dx = -40.0;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        gdouble sx = 0.0, sy = 0.0;
        gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &sx, &sy);
        dx = -sx * 30.0;
        dy = -sy * 30.0;
    }

    panBy(dx, dy);
    return TRUE;
}

gboolean WorkspaceView::onButtonPress(GdkEventButton* event) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);

    if (minimapHitTest(event->x, event->y, alloc.width, alloc.height)) {
        if (event->button == GDK_BUTTON_PRIMARY) {
            m_isMinimapDragging = true;
            handleMinimapInteraction(event->x, event->y, alloc.width, alloc.height);
            return TRUE;
        }
    }

    if (event->button == GDK_BUTTON_MIDDLE ||
        (event->button == GDK_BUTTON_PRIMARY && ((event->state & GDK_MOD1_MASK) || m_currentTool == "pan"))) {
        m_isPanning = true;
        m_lastMouseX = event->x;
        m_lastMouseY = event->y;

        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            GdkDisplay* display = gdk_window_get_display(win);
            GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
            gdk_window_set_cursor(win, cursor);
            if (cursor)
                g_object_unref(cursor);
        }
        return TRUE;
    }

    if (event->button == GDK_BUTTON_PRIMARY) {
        if (event->type == GDK_2BUTTON_PRESS) {
            FluidCore::Point worldPt = screenToWorld(event->x, event->y);
            centerOn(worldPt.x, worldPt.y);
            return TRUE;
        }

        if (m_currentTool == "pen" || m_currentTool == "highlighter") {
            m_isDrawing = true;
            m_activeStroke = FluidCore::Stroke{};
            
            static std::size_t s_strokeCounter = 1;
            m_activeStroke.id = "stroke-" + std::to_string(s_strokeCounter++);
            
            m_activeStroke.tool = m_currentTool;
            m_activeStroke.color = m_currentColor;
            m_activeStroke.width = m_currentWidth;
            m_activeStroke.timestamp = g_get_real_time();
            m_activeSegments.clear();
            m_hasWetSegment = false;
            
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            m_stabilizer.beginStroke(FluidCoreApp::StrokeStabilizer::Point2D{wPt.x, wPt.y}, 1.0, g_get_real_time());
            gtk_widget_queue_draw(m_area);
            return TRUE;
        } else if (m_currentTool == "eraser") {
            m_isDrawing = true;
            m_lastMouseX = event->x;
            m_lastMouseY = event->y;
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            const double wRadius = 30.0 / m_zoom; // 30 screen pixels
            const FluidCore::Rectangle queryRect{wPt.x - wRadius, wPt.y - wRadius, wRadius * 2.0, wRadius * 2.0};
            
            auto hits = m_api.queryVisibleNodes(queryRect);
            bool removed = false;
            for (const auto* hit : hits) {
                if (const auto* strokeNode = dynamic_cast<const FluidCore::CanvasStrokeNode*>(hit)) {
                    m_api.removeNode(strokeNode->id());
                    removed = true;
                }
            }
            if (removed) {
                gtk_widget_queue_draw(m_area);
            }
            return TRUE;
        }
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
            gdk_window_set_cursor(win, nullptr);
        }
        return TRUE;
    }

    if (m_isDrawing) {
        m_isDrawing = false;
        if (m_currentTool == "pen" || m_currentTool == "highlighter") {
            m_stabilizer.endStroke();
            m_hasWetSegment = false;
            
            // Reconstruct FluidCore::Stroke points from stabilizer raw samples
            m_activeStroke.points.clear();
            for (const auto& sample : m_stabilizer.rawSamples()) {
                m_activeStroke.points.push_back(FluidCore::XoppPoint{sample.point.x, sample.point.y});
            }
            
            if (!m_activeStroke.points.empty()) {
                m_api.insertNode(std::make_unique<FluidCore::CanvasStrokeNode>(m_activeStroke));
            }
            m_activeSegments.clear();
            gtk_widget_queue_draw(m_area);
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

    if (m_isDrawing) {
        FluidCore::Point wPt = screenToWorld(event->x, event->y);
        
        if (m_currentTool == "pen" || m_currentTool == "highlighter") {
            auto result = m_stabilizer.pushPoint(FluidCoreApp::StrokeStabilizer::Point2D{wPt.x, wPt.y}, 1.0, g_get_real_time());
            for (const auto& seg : result.newlyCommitted) {
                m_activeSegments.push_back(seg);
            }
            m_hasWetSegment = result.hasWetSegment;
            m_activeWetTip = result.wetTip;
            gtk_widget_queue_draw(m_area);
        } else if (m_currentTool == "eraser") {
            const double wRadius = 30.0 / m_zoom; // 30 screen pixels
            const FluidCore::Rectangle queryRect{wPt.x - wRadius, wPt.y - wRadius, wRadius * 2.0, wRadius * 2.0};
            
            auto hits = m_api.queryVisibleNodes(queryRect);
            bool removed = false;
            for (const auto* hit : hits) {
                if (const auto* strokeNode = dynamic_cast<const FluidCore::CanvasStrokeNode*>(hit)) {
                    m_api.removeNode(strokeNode->id());
                    removed = true;
                }
            }
            if (removed) {
                gtk_widget_queue_draw(m_area);
            }
        }
        return TRUE;
    }

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
    switch (event->keyval) {
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add: {
        GtkAllocation alloc;
        gtk_widget_get_allocation(m_area, &alloc);
        zoomAt(1.2, alloc.width / 2.0, alloc.height / 2.0);
        return TRUE;
    }
    case GDK_KEY_minus:
    case GDK_KEY_underscore:
    case GDK_KEY_KP_Subtract: {
        GtkAllocation alloc;
        gtk_widget_get_allocation(m_area, &alloc);
        zoomAt(1.0 / 1.2, alloc.width / 2.0, alloc.height / 2.0);
        return TRUE;
    }
    case GDK_KEY_0:
    case GDK_KEY_KP_0: {
        if (event->state & GDK_CONTROL_MASK) {
            resetView();
            return TRUE;
        }
        break;
    }
    case GDK_KEY_m:
    case GDK_KEY_M: {
        if (event->state & GDK_CONTROL_MASK) {
            setMinimapVisible(!m_showMinimap);
            return TRUE;
        }
        break;
    }
    case GDK_KEY_Left:
        panBy(50.0, 0.0);
        return TRUE;
    case GDK_KEY_Right:
        panBy(-50.0, 0.0);
        return TRUE;
    case GDK_KEY_Up:
        panBy(0.0, 50.0);
        return TRUE;
    case GDK_KEY_Down:
        panBy(0.0, -50.0);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

void WorkspaceView::drawBackgroundGrid(cairo_t* cr, int width, int height) {
    double dynamicGridStep = kBaseGridStep * m_zoom;
    while (dynamicGridStep < 16.0)
        dynamicGridStep *= 2.0;
    while (dynamicGridStep > 64.0)
        dynamicGridStep /= 2.0;

    const double worldGridStep = dynamicGridStep / m_zoom;
    const double startWorldX = std::floor(m_originX / worldGridStep) * worldGridStep;
    const double startWorldY = std::floor(m_originY / worldGridStep) * worldGridStep;

    const double screenStartX = (startWorldX - m_originX) * m_zoom;
    const double screenStartY = (startWorldY - m_originY) * m_zoom;
    const double dotRadius = std::clamp(1.1 * std::min(1.0, m_zoom), 0.7, 1.8);

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

    if (m_zoom >= 0.3) {
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

void WorkspaceView::drawMinimap(cairo_t* cr, int width, int height) {
    if (!m_showMinimap)
        return;

    const FluidCore::Rectangle mm = getMinimapRect(width, height);
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

    cairo_save(cr);
    drawRoundedRect(cr, mm.x + 1.0, mm.y + 1.0, mm.w - 2.0, mm.h - 2.0, 7.0);
    cairo_clip(cr);

    // Miniature nodes
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

    // Active viewport frame indicator
    const FluidCore::Point vp1 = worldToMinimap(m_originX, m_originY);
    const FluidCore::Point vp2 = worldToMinimap(m_originX + currentViewW, m_originY + currentViewH);
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
    oss << static_cast<int>(std::round(m_zoom * 100.0)) << "%";
    const std::string zoomStr = oss.str();
    cairo_text_extents_t ext;
    cairo_text_extents(cr, zoomStr.c_str(), &ext);
    cairo_move_to(cr, mm.x + mm.w - ext.width - 8.0, mm.y + 14.0);
    cairo_show_text(cr, zoomStr.c_str());

    cairo_restore(cr);
}

void WorkspaceView::drawExcerptCard(cairo_t* cr, const FluidCore::WorkspaceNode* node, double sx,
                                    double sy, double sw, double sh) {
    const auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node);
    const double radius = 8.0 * std::min(1.0, m_zoom);

    // 1. Soft elevation card shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.07);
    drawRoundedRect(cr, sx + 2.0, sy + 4.0, sw, sh, radius);
    cairo_fill(cr);

    // 2. Card background container
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);

    // 3. Card border
    cairo_set_source_rgb(cr, 0.80, 0.85, 0.92);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);

    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_clip(cr);

    const double headerH = std::min(28.0 * m_zoom, sh * 0.35);

    // 4. Header background bar
    cairo_rectangle(cr, sx, sy, sw, headerH);
    cairo_set_source_rgb(cr, 0.96, 0.97, 0.99);
    cairo_fill(cr);

    // Header divider line
    cairo_move_to(cr, sx, sy + headerH);
    cairo_line_to(cr, sx + sw, sy + headerH);
    cairo_set_source_rgb(cr, 0.88, 0.91, 0.95);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // 5. Left accent indicator bar
    if (excerpt) {
        const auto col = excerpt->color();
        cairo_set_source_rgba(cr, col.r / 255.0, col.g / 255.0, col.b / 255.0, 0.9);
        const double barW = std::max(4.0 * m_zoom, 3.0);
        cairo_rectangle(cr, sx, sy, barW, sh);
        cairo_fill(cr);
    }

    if (m_zoom >= 0.2) {
        // 6. Header Badge (Document Name & Page Number)
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
        cairo_set_font_size(cr, std::clamp(10.5 * m_zoom, 8.0, 13.0));
        cairo_set_source_rgb(cr, 0.25, 0.32, 0.42);
        cairo_move_to(cr, sx + 12.0 * m_zoom, sy + headerH * 0.68);
        cairo_show_text(cr, headerStr.c_str());

        // 7. Return Anchor Pill on header right
        if (excerpt) {
            const double pillW = 62.0 * m_zoom;
            const double pillH = 18.0 * m_zoom;
            const double pillX = sx + sw - pillW - 8.0 * m_zoom;
            const double pillY = sy + (headerH - pillH) / 2.0;

            if (pillW > 20.0) {
                drawRoundedRect(cr, pillX, pillY, pillW, pillH, pillH / 2.0);
                cairo_set_source_rgba(cr, 0.05, 0.45, 0.90, 0.08);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.05, 0.45, 0.90, 0.35);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);

                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, std::clamp(8.5 * m_zoom, 7.0, 10.5));
                cairo_set_source_rgb(cr, 0.05, 0.40, 0.85);
                cairo_move_to(cr, pillX + 6.0 * m_zoom, pillY + pillH * 0.72);
                cairo_show_text(cr, "← Anchor");
            }
        }
    }

    // 8. Body Content Rendering
    if (excerpt && m_zoom >= 0.25) {
        if (excerpt->isImageExcerpt()) {
            const double bodyX = sx + 12.0 * m_zoom;
            const double bodyY = sy + headerH + 8.0 * m_zoom;
            const double bodyW = sw - 24.0 * m_zoom;
            const double bodyH = sh - headerH - 16.0 * m_zoom;

            if (bodyW > 10.0 && bodyH > 10.0) {
                drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                cairo_set_source_rgb(cr, 0.93, 0.95, 0.98);
                cairo_fill_preserve(cr);

                cairo_set_source_rgb(cr, 0.75, 0.80, 0.88);
                double dashes[] = {4.0, 4.0};
                cairo_set_dash(cr, dashes, 2, 0.0);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
                cairo_set_dash(cr, nullptr, 0, 0.0);

                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, std::clamp(10.0 * m_zoom, 8.0, 12.0));
                cairo_set_source_rgb(cr, 0.45, 0.52, 0.62);
                cairo_text_extents_t ext;
                cairo_text_extents(cr, "Visual Diagram Crop", &ext);
                cairo_move_to(cr, bodyX + (bodyW - ext.width) / 2.0,
                              bodyY + (bodyH + ext.height) / 2.0);
                cairo_show_text(cr, "Visual Diagram Crop");
            }
        } else {
            const double textStartX = sx + 14.0 * m_zoom;
            double curY = sy + headerH + 18.0 * m_zoom;
            const double maxW = sw - 28.0 * m_zoom;
            const double fontSize = std::clamp(11.0 * m_zoom, 8.0, 14.0);
            const double lineSpacing = 16.0 * m_zoom;

            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, fontSize);
            cairo_set_source_rgb(cr, 0.12, 0.17, 0.24);

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
                    std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
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

    cairo_restore(cr);
}

void WorkspaceView::drawGenericNode(cairo_t* cr, const FluidCore::WorkspaceNode* node, double sx,
                                    double sy, double sw, double sh) {
    const double radius = 8.0 * std::min(1.0, m_zoom);

    // Card shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx + 2.0, sy + 3.0, sw, sh, radius);
    cairo_fill(cr);

    // Card background
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.80, 0.85, 0.90, 0.9);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Top accent bar
    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, 6.0 * m_zoom, radius);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, 0.25, 0.55, 0.90);
    cairo_rectangle(cr, sx, sy, sw, 6.0 * m_zoom);
    cairo_fill(cr);
    cairo_restore(cr);

    // Title / ID text
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, std::clamp(11.0 * m_zoom, 8.0, 14.0));
    cairo_set_source_rgb(cr, 0.20, 0.25, 0.35);
    cairo_move_to(cr, sx + 12.0 * m_zoom, sy + 24.0 * m_zoom);
    cairo_show_text(cr, node->id().c_str());
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
        if (const auto* card = dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - m_originX) * m_zoom;
            const double sy = (b.y - m_originY) * m_zoom;
            const double sw = b.w * m_zoom;
            const double sh = b.h * m_zoom;
            drawExcerptCard(cr, node, sx, sy, sw, sh);
        } else if (const auto* strokeNode = dynamic_cast<const FluidCore::CanvasStrokeNode*>(node)) {
            const auto& stroke = strokeNode->stroke();
            if (stroke.points.empty()) continue;

            cairo_set_source_rgba(cr, ((stroke.color >> 16) & 0xFF) / 255.0,
                                  ((stroke.color >> 8) & 0xFF) / 255.0,
                                  (stroke.color & 0xFF) / 255.0,
                                  stroke.tool == "highlighter" ? 0.45 : 1.0);
            cairo_set_line_width(cr, stroke.width * m_zoom);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

            cairo_new_path(cr);
            const auto& pt0 = stroke.points[0];
            cairo_move_to(cr, (pt0.x - m_originX) * m_zoom, (pt0.y - m_originY) * m_zoom);
            
            if (stroke.points.size() == 1) {
                cairo_arc(cr, (pt0.x - m_originX) * m_zoom, (pt0.y - m_originY) * m_zoom,
                          std::max(1.0, stroke.width * m_zoom / 2.0), 0, 2 * M_PI);
                cairo_fill(cr);
            } else {
                for (size_t i = 1; i < stroke.points.size(); ++i) {
                    const auto& pt = stroke.points[i];
                    cairo_line_to(cr, (pt.x - m_originX) * m_zoom, (pt.y - m_originY) * m_zoom);
                }
                cairo_stroke(cr);
            }
        } else {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - m_originX) * m_zoom;
            const double sy = (b.y - m_originY) * m_zoom;
            const double sw = b.w * m_zoom;
            const double sh = b.h * m_zoom;
            drawGenericNode(cr, node, sx, sy, sw, sh);
        }
    }

    // Render active wet ink
    if (m_isDrawing && (m_currentTool == "pen" || m_currentTool == "highlighter")) {
        cairo_set_source_rgba(cr, ((m_currentColor >> 16) & 0xFF) / 255.0,
                              ((m_currentColor >> 8) & 0xFF) / 255.0,
                              (m_currentColor & 0xFF) / 255.0,
                              m_currentTool == "highlighter" ? 0.45 : 1.0);
        cairo_set_line_width(cr, m_currentWidth * m_zoom);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_new_path(cr);

        const auto& samples = m_stabilizer.rawSamples();
        if (samples.size() == 1) {
            cairo_arc(cr, (samples[0].point.x - m_originX) * m_zoom,
                      (samples[0].point.y - m_originY) * m_zoom,
                      std::max(1.0, m_currentWidth * m_zoom / 2.0), 0, 2 * M_PI);
            cairo_fill(cr);
        } else if (samples.size() > 1) {
            cairo_move_to(cr, (samples[0].point.x - m_originX) * m_zoom,
                          (samples[0].point.y - m_originY) * m_zoom);
            for (size_t i = 1; i < samples.size(); ++i) {
                cairo_line_to(cr, (samples[i].point.x - m_originX) * m_zoom,
                              (samples[i].point.y - m_originY) * m_zoom);
            }
            cairo_stroke(cr);
        }
    }

    // Render drop target ghost box during active drag hover
    if (m_isDropHovering) {
        const double ghostW = 260.0 * m_zoom;
        const double ghostH = 140.0 * m_zoom;
        const double gx = m_dropHoverScreenX;
        const double gy = m_dropHoverScreenY;

        cairo_save(cr);
        drawRoundedRect(cr, gx, gy, ghostW, ghostH, 8.0 * std::min(1.0, m_zoom));
        cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, 0.12);
        cairo_fill_preserve(cr);

        cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, 0.85);
        double dashes[] = {6.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_set_line_width(cr, 1.8);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Render floating minimap HUD
    drawMinimap(cr, width, height);
}

} // namespace FluidCoreApp
