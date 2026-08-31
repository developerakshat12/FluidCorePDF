#include "WorkspaceView.h"
#include "FluidCoreEngine.h"
#include "graph/GraphTopology.h"
#include "undo/WorkspaceCommands.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/ExcerptPayload.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace FluidCoreApp {
namespace {

constexpr double kMinZoom = 0.10; // 10%
constexpr double kMaxZoom = 2.0;  // 200%
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

std::pair<double, double>
computeExcerptCardDimensions(const FluidCore::ExcerptDropPayload& payload) {
    if (!payload.isImageExcerpt) {
        double cardW = 260.0;
        double cardH = 140.0;
        if (payload.textSnippet.size() > 250) {
            cardH = 220.0;
        } else if (payload.textSnippet.size() > 120) {
            cardH = 170.0;
        }
        return {cardW, cardH};
    }

    // Intentional defensive fallback for in-memory payloads that bypass string deserialization
    const double pw = (payload.sourcePageWidth > 0.0) ? payload.sourcePageWidth : 612.0;
    const double ph = (payload.sourcePageHeight > 0.0) ? payload.sourcePageHeight : 792.0;

    const double cropW_pt = std::max(1.0, payload.sourceNormalizedRect.w * pw);
    const double cropH_pt = std::max(1.0, payload.sourceNormalizedRect.h * ph);

    // Uniform scalar sizing to guarantee W_img / H_img == cropW_pt / cropH_pt == AR
    constexpr double kMaxInnerW = 450.0;
    constexpr double kMaxInnerH = 380.0;
    constexpr double kMinInnerTarget = 180.0;

    double s = std::min(1.0, std::min(kMaxInnerW / cropW_pt, kMaxInnerH / cropH_pt));
    const double maxDim = std::max(cropW_pt, cropH_pt);
    if (maxDim < 160.0) {
        const double upscale = kMinInnerTarget / maxDim;
        s = std::min(upscale, std::min(kMaxInnerW / cropW_pt, kMaxInnerH / cropH_pt));
    }

    const double imgW = s * cropW_pt;
    const double imgH = s * cropH_pt;

    // Outer card container: width floor of 200pt ensures title and [ ↗ Anchor ] pill fit cleanly
    const double cardW = std::max(200.0, imgW + 20.0);
    const double cardH = imgH + 46.0; // 28pt header + 6pt top gap + 12pt bottom margin

    return {cardW, cardH};
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
    g_signal_connect(m_area, "key-release-event", G_CALLBACK(WorkspaceView::keyReleaseCallback),
                     this);

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

WorkspaceView::~WorkspaceView() {
    if (m_zoomSettlingTimerId != 0) {
        g_source_remove(m_zoomSettlingTimerId);
        m_zoomSettlingTimerId = 0;
    }
    if (m_glideTimerId != 0) {
        g_source_remove(m_glideTimerId);
        m_glideTimerId = 0;
    }
    if (m_flashTimerId != 0) {
        g_source_remove(m_flashTimerId);
        m_flashTimerId = 0;
    }
}

void WorkspaceView::setExcerptTileCache(ExcerptTileCache* cache) {
    m_excerptTileCache = cache;
    if (m_excerptTileCache) {
        m_excerptTileCache->setRenderReadyCallback(
            [this](const std::string& /*excerptId*/, uint64_t /*requestId*/) {
                if (m_area && GTK_IS_WIDGET(m_area)) {
                    gtk_widget_queue_draw(m_area);
                }
            });
    }
}

gboolean WorkspaceView::zoomSettlingTimeoutCallback(gpointer userData) {
    auto* self = static_cast<WorkspaceView*>(userData);
    if (self) {
        self->m_zoomSettlingTimerId = 0;
        self->onZoomSettled();
    }
    return G_SOURCE_REMOVE;
}

void WorkspaceView::onZoomSettled() {
    if (!m_excerptTileCache || !m_area) {
        return;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const FluidCore::Rectangle viewport{m_originX, m_originY, alloc.width / m_zoom,
                                        alloc.height / m_zoom};

    auto visibleNodes = m_api.queryVisibleNodes(viewport);
    for (const auto* node : visibleNodes) {
        const auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node);
        if (excerpt && excerpt->isImageExcerpt()) {
            m_excerptTileCache->requestCropAsync(
                excerpt->id(), excerpt->sourceDocId(), excerpt->sourcePageNo(),
                excerpt->sourceNormalizedRect(), excerpt->bounds().w - 16.0,
                excerpt->bounds().h - 40.0, m_zoom);
        }
    }
    gtk_widget_queue_draw(m_area);
}

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

    if (m_zoomSettlingTimerId != 0) {
        g_source_remove(m_zoomSettlingTimerId);
        m_zoomSettlingTimerId = 0;
    }
    m_zoomSettlingTimerId = g_timeout_add(150, zoomSettlingTimeoutCallback, this);

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

void WorkspaceView::glideToWorldCoord(double targetWorldX, double targetWorldY) {
    if (m_glideTimerId != 0) {
        g_source_remove(m_glideTimerId);
        m_glideTimerId = 0;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double vw = alloc.width > 0 ? alloc.width / m_zoom : 800.0;
    const double vh = alloc.height > 0 ? alloc.height / m_zoom : 600.0;

    m_glideStartX = m_originX;
    m_glideStartY = m_originY;
    m_glideTargetX = targetWorldX - vw / 2.0;
    m_glideTargetY = targetWorldY - vh / 2.0;
    m_glideStartTimeUs = g_get_real_time();

    m_glideTimerId = g_timeout_add(
        16,
        +[](gpointer data) -> gboolean {
            auto* self = static_cast<WorkspaceView*>(data);
            if (!self) {
                return G_SOURCE_REMOVE;
            }

            const gint64 elapsedUs = g_get_real_time() - self->m_glideStartTimeUs;
            const double elapsedSec = static_cast<double>(elapsedUs) / 1000000.0;
            const double totalDurationSec = 0.25;

            if (elapsedSec >= totalDurationSec) {
                self->m_originX = self->m_glideTargetX;
                self->m_originY = self->m_glideTargetY;
                self->m_glideTimerId = 0;
                if (self->m_area) {
                    gtk_widget_queue_draw(self->m_area);
                }
                return G_SOURCE_REMOVE;
            }

            // Cubic ease-out: 1 - (1 - t)^3
            const double t = elapsedSec / totalDurationSec;
            const double easeOut = 1.0 - std::pow(1.0 - t, 3.0);

            self->m_originX =
                self->m_glideStartX + (self->m_glideTargetX - self->m_glideStartX) * easeOut;
            self->m_originY =
                self->m_glideStartY + (self->m_glideTargetY - self->m_glideStartY) * easeOut;

            if (self->m_area) {
                gtk_widget_queue_draw(self->m_area);
            }
            return G_SOURCE_CONTINUE;
        },
        this);
}

void WorkspaceView::flashExcerptCard(const std::string& cardId) {
    if (m_flashTimerId != 0) {
        g_source_remove(m_flashTimerId);
        m_flashTimerId = 0;
    }

    // Auto-expand stack if target card is nested inside a collapsed stack
    const FluidCore::Rectangle wsBounds = m_api.getWorkspaceBounds();
    const auto allNodes = m_api.queryVisibleNodes(wsBounds);
    for (const auto* node : allNodes) {
        if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            if (stack->isCollapsed() && stack->containsChild(cardId)) {
                m_api.setStackCollapsed(stack->id(), false);
                break;
            }
        }
    }

    m_flashCardId = cardId;
    m_flashAlpha = 1.0;
    m_flashStartTimeUs = g_get_real_time();

    m_flashTimerId = g_timeout_add(
        16,
        +[](gpointer data) -> gboolean {
            auto* self = static_cast<WorkspaceView*>(data);
            if (!self) {
                return G_SOURCE_REMOVE;
            }

            const gint64 elapsedUs = g_get_real_time() - self->m_flashStartTimeUs;
            const double elapsedSec = static_cast<double>(elapsedUs) / 1000000.0;
            const double totalDurationSec = 1.2;

            if (elapsedSec >= totalDurationSec) {
                self->m_flashCardId.clear();
                self->m_flashAlpha = 0.0;
                self->m_flashTimerId = 0;
                if (self->m_area) {
                    gtk_widget_queue_draw(self->m_area);
                }
                return G_SOURCE_REMOVE;
            }

            const double progress = elapsedSec / totalDurationSec;
            self->m_flashAlpha = (1.0 - progress) * (1.0 - progress);

            if (self->m_area) {
                gtk_widget_queue_draw(self->m_area);
            }
            return G_SOURCE_CONTINUE;
        },
        this);

    if (m_area) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::setSpacePressed(bool pressed) {
    if (m_isSpacePressed == pressed)
        return;
    m_isSpacePressed = pressed;
    if (m_area && GTK_IS_WIDGET(m_area)) {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            if (m_isSpacePressed && !m_isPanning) {
                GdkDisplay* display = gdk_window_get_display(win);
                GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND1);
                gdk_window_set_cursor(win, cursor);
                if (cursor)
                    g_object_unref(cursor);
            } else if (!m_isSpacePressed && !m_isPanning) {
                gdk_window_set_cursor(win, nullptr);
            }
        }
    }
}

FluidCore::Rectangle
WorkspaceView::getExcerptAnchorPillRect(const FluidCore::WorkspaceNode* node) const {
    if (!node) {
        return {0.0, 0.0, 0.0, 0.0};
    }
    const auto bounds = node->bounds();
    const FluidCore::Point sp = worldToScreen(bounds.x, bounds.y);
    const double sw = bounds.w * m_zoom;
    const double sh = bounds.h * m_zoom;
    const double headerH = std::min(28.0 * m_zoom, sh * 0.35);
    const double pillW = 72.0 * m_zoom;
    const double pillH = 20.0 * m_zoom;
    const double pillX = sp.x + sw - pillW - 8.0 * m_zoom;
    const double pillY = sp.y + (headerH - pillH) / 2.0;
    return {pillX, pillY, pillW, pillH};
}

FluidCore::Rectangle WorkspaceView::getStackHeaderRect(const FluidCore::WorkspaceNode* node) const {
    if (!node) {
        return {0.0, 0.0, 0.0, 0.0};
    }
    const auto b = node->bounds();
    const FluidCore::Point sp = worldToScreen(b.x, b.y);
    const double sw = b.w * m_zoom;
    const double headerH = FluidCore::CardStackNode::kHeaderHeight * m_zoom;
    return {sp.x, sp.y, sw, headerH};
}

FluidCore::Rectangle
WorkspaceView::getStackChevronRect(const FluidCore::WorkspaceNode* node) const {
    if (!node) {
        return {0.0, 0.0, 0.0, 0.0};
    }
    const auto b = node->bounds();
    const FluidCore::Point sp = worldToScreen(b.x, b.y);
    const double headerH = FluidCore::CardStackNode::kHeaderHeight * m_zoom;
    const double btnSize = std::min(24.0 * m_zoom, headerH);
    const double btnX = sp.x + 6.0 * m_zoom;
    const double btnY = sp.y + (headerH - btnSize) / 2.0;
    return {btnX, btnY, btnSize, btnSize};
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

gboolean WorkspaceView::keyReleaseCallback(GtkWidget*, GdkEventKey* event, gpointer userData) {
    return static_cast<WorkspaceView*>(userData)->onKeyRelease(event);
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

        const auto [cardW, cardH] = computeExcerptCardDimensions(payload);
        FluidCore::Rectangle cardBounds{dropWorld.x, dropWorld.y, cardW, cardH};
        uint64_t timestamp = static_cast<uint64_t>(time);

        auto card = std::make_unique<FluidCore::ExcerptCardNode>(
            cardId, cardBounds, payload.sourceDocId, payload.sourcePageNo,
            payload.sourceNormalizedRect, payload.textSnippet, payload.isImageExcerpt,
            payload.color, timestamp);

        if (m_onExcerptAdded) {
            m_onExcerptAdded(*card);
        }

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
            delta = std::clamp(static_cast<double>(dy), -2.5, 2.5);
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
        (event->button == GDK_BUTTON_PRIMARY &&
         (m_isSpacePressed || (event->state & GDK_MOD1_MASK) || m_currentTool == "pan"))) {
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

    if (event->button == GDK_BUTTON_SECONDARY) {
        // Right-click: hit-test edge or node for deletion context menu
        FluidCore::Point wPt = screenToWorld(event->x, event->y);
        std::string hitEdge = hitTestEdgeAtWorldPoint(wPt, 10.0);
        if (!hitEdge.empty()) {
            m_selectedEdgeId = hitEdge;
            m_selectedNodeId.reset();
            gtk_widget_queue_draw(m_area);

            GtkWidget* menu = gtk_menu_new();
            GtkWidget* deleteItem = gtk_menu_item_new_with_label("Delete Connector");
            g_signal_connect(deleteItem, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
                                 auto* self = static_cast<WorkspaceView*>(data);
                                 if (self && self->m_selectedEdgeId) {
                                     self->m_api.removeEdge(*self->m_selectedEdgeId);
                                     self->m_selectedEdgeId.reset();
                                     gtk_widget_queue_draw(self->m_area);
                                 }
                             }),
                             this);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), deleteItem);
            gtk_widget_show_all(menu);
            gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
            return TRUE;
        }

        const auto* hitNode = hitTestNodeAtWorldPoint(wPt);
        if (hitNode) {
            m_selectedNodeId = hitNode->id();
            m_selectedEdgeId.reset();
            gtk_widget_queue_draw(m_area);

            GtkWidget* menu = gtk_menu_new();
            const bool isStack =
                (dynamic_cast<const FluidCore::CardStackNode*>(hitNode) != nullptr);
            GtkWidget* deleteItem =
                gtk_menu_item_new_with_label(isStack ? "Delete Stack" : "Delete Card");
            g_signal_connect(deleteItem, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
                                 auto* self = static_cast<WorkspaceView*>(data);
                                 if (self && self->m_selectedNodeId) {
                                     self->m_api.removeNode(*self->m_selectedNodeId);
                                     self->m_selectedNodeId.reset();
                                     gtk_widget_queue_draw(self->m_area);
                                 }
                             }),
                             this);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), deleteItem);
            gtk_widget_show_all(menu);
            gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
            return TRUE;
        }
    }

    if (event->button == GDK_BUTTON_PRIMARY) {
        const FluidCore::Point worldTopLeft = screenToWorld(0, 0);
        const FluidCore::Point worldBottomRight = screenToWorld(
            alloc.width > 0 ? alloc.width : 800, alloc.height > 0 ? alloc.height : 600);
        const FluidCore::Rectangle viewWorldRect{
            worldTopLeft.x, worldTopLeft.y, std::max(0.0, worldBottomRight.x - worldTopLeft.x),
            std::max(0.0, worldBottomRight.y - worldTopLeft.y)};

        auto visibleNodes = m_api.queryVisibleNodes(viewWorldRect);

        // 1. Check if click hits Stack Chevron button [▼]/[▶] -> Toggle Collapse
        for (const auto* node : visibleNodes) {
            if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                const auto chevRect = getStackChevronRect(stack);
                if (event->x >= chevRect.x && event->x <= chevRect.x + chevRect.w &&
                    event->y >= chevRect.y && event->y <= chevRect.y + chevRect.h) {
                    m_api.toggleStackCollapsed(stack->id());
                    gtk_widget_queue_draw(m_area);
                    return TRUE;
                }
            }
        }

        // 2. Check if click hits any visible ExcerptCardNode's [ ↗ Anchor ] pill button (top-level
        // or inside stack)
        if (m_zoom >= 0.2) {
            for (const auto* node : visibleNodes) {
                if (const auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
                    const auto pillRect = getExcerptAnchorPillRect(node);
                    if (event->x >= pillRect.x && event->x <= pillRect.x + pillRect.w &&
                        event->y >= pillRect.y && event->y <= pillRect.y + pillRect.h) {
                        const auto b = excerpt->bounds();
                        const FluidCore::Point centerWorldPt{b.x + b.w / 2.0, b.y + b.h / 2.0};
                        if (m_onNavigateToSource) {
                            m_onNavigateToSource(excerpt->sourceDocId(), excerpt->sourcePageNo(),
                                                 excerpt->sourceNormalizedRect(), excerpt->id(),
                                                 excerpt->textSnippet(), centerWorldPt);
                        }
                        return TRUE;
                    }
                } else if (const auto* stack =
                               dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                    if (!stack->isCollapsed()) {
                        for (const auto& child : stack->children()) {
                            if (const auto* cExcerpt =
                                    dynamic_cast<const FluidCore::ExcerptCardNode*>(child.get())) {
                                const auto pillRect = getExcerptAnchorPillRect(cExcerpt);
                                if (event->x >= pillRect.x && event->x <= pillRect.x + pillRect.w &&
                                    event->y >= pillRect.y && event->y <= pillRect.y + pillRect.h) {
                                    const auto b = cExcerpt->bounds();
                                    const FluidCore::Point centerWorldPt{b.x + b.w / 2.0,
                                                                         b.y + b.h / 2.0};
                                    if (m_onNavigateToSource) {
                                        m_onNavigateToSource(
                                            cExcerpt->sourceDocId(), cExcerpt->sourcePageNo(),
                                            cExcerpt->sourceNormalizedRect(), cExcerpt->id(),
                                            cExcerpt->textSnippet(), centerWorldPt);
                                    }
                                    return TRUE;
                                }
                            }
                        }
                    }
                }
            }
        }

        // 3. Double-Click: If on stack header -> toggle collapse; otherwise center on point
        if (event->type == GDK_2BUTTON_PRESS) {
            for (const auto* node : visibleNodes) {
                if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                    const auto hdrRect = getStackHeaderRect(stack);
                    if (event->x >= hdrRect.x && event->x <= hdrRect.x + hdrRect.w &&
                        event->y >= hdrRect.y && event->y <= hdrRect.y + hdrRect.h) {
                        m_api.toggleStackCollapsed(stack->id());
                        gtk_widget_queue_draw(m_area);
                        return TRUE;
                    }
                }
            }

            FluidCore::Point worldPt = screenToWorld(event->x, event->y);
            centerOn(worldPt.x, worldPt.y);
            return TRUE;
        }

        const bool isDrawingOrConnecting =
            (m_currentTool == "pen" || m_currentTool == "highlighter" ||
             m_currentTool == "eraser" || m_currentTool == "connector");

        if (!isDrawingOrConnecting) {
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            if (m_currentTool == "select") {
                std::string hitEdge = hitTestEdgeAtWorldPoint(wPt, 8.0);
                if (!hitEdge.empty()) {
                    m_selectedEdgeId = hitEdge;
                    m_selectedNodeId.reset();
                    gtk_widget_queue_draw(m_area);
                    return TRUE;
                } else {
                    if (m_selectedEdgeId.has_value()) {
                        m_selectedEdgeId.reset();
                        gtk_widget_queue_draw(m_area);
                    }
                }
            }

            // Hit test child node inside expanded stack first (for extraction)
            std::string parentStackId;
            const auto* hitChild = hitTestChildNodeAtWorldPoint(wPt, &parentStackId);
            if (hitChild && !parentStackId.empty()) {
                m_dragPending = true;
                m_dragStartScreenX = event->x;
                m_dragStartScreenY = event->y;
                m_dragCandidateNodeId = hitChild->id();
                m_dragCandidateIsChild = true;
                m_dragCandidateParentStackId = parentStackId;
                const auto b = hitChild->bounds();
                m_dragInitialWorldPos = {b.x, b.y};
                m_dragOffsetWorld = {wPt.x - b.x, wPt.y - b.y};
                m_selectedNodeId = hitChild->id();
                gtk_widget_queue_draw(m_area);
                return TRUE;
            }

            // Hit test top-level node
            const auto* hitNode = hitTestNodeAtWorldPoint(wPt);
            if (hitNode) {
                m_dragPending = true;
                m_dragStartScreenX = event->x;
                m_dragStartScreenY = event->y;
                m_dragCandidateNodeId = hitNode->id();
                m_dragCandidateIsChild = false;
                m_dragCandidateParentStackId.clear();
                m_dragInitialWorldPos = m_api.getNodePosition(hitNode->id());
                m_dragOffsetWorld = {wPt.x - m_dragInitialWorldPos.x,
                                     wPt.y - m_dragInitialWorldPos.y};
                m_selectedNodeId = hitNode->id();
                gtk_widget_queue_draw(m_area);
                return TRUE;
            } else {
                if (m_selectedNodeId.has_value()) {
                    m_selectedNodeId.reset();
                    gtk_widget_queue_draw(m_area);
                }
            }
        }

        if (m_currentTool == "connector") {
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            const auto* hitNode = hitTestNodeAtWorldPoint(wPt);
            if (hitNode) {
                m_isConnecting = true;
                m_connectorSourceNodeId = hitNode->id();
                m_connectorStartWorld = wPt;
                m_connectorCurrentWorld = wPt;
                m_connectorTargetHoverNodeId.clear();
                gtk_widget_queue_draw(m_area);
                return TRUE;
            }
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
            m_stabilizer.beginStroke(FluidCoreApp::StrokeStabilizer::Point2D{wPt.x, wPt.y}, 1.0,
                                     g_get_real_time());
            gtk_widget_queue_draw(m_area);
            return TRUE;
        } else if (m_currentTool == "eraser") {
            m_isDrawing = true;
            m_lastMouseX = event->x;
            m_lastMouseY = event->y;
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            const double wRadius = 30.0 / m_zoom; // 30 screen pixels
            const FluidCore::Rectangle queryRect{wPt.x - wRadius, wPt.y - wRadius, wRadius * 2.0,
                                                 wRadius * 2.0};

            auto hits = m_api.queryVisibleNodes(queryRect);
            bool removed = false;
            for (const auto* hit : hits) {
                if (const auto* strokeNode =
                        dynamic_cast<const FluidCore::CanvasStrokeNode*>(hit)) {
                    m_api.removeNode(strokeNode->id());
                    removed = true;
                }
            }

            // Eraser edge deletion
            std::string hitEdge = hitTestEdgeAtWorldPoint(wPt, wRadius);
            if (!hitEdge.empty()) {
                m_api.removeEdge(hitEdge);
                if (m_selectedEdgeId && *m_selectedEdgeId == hitEdge) {
                    m_selectedEdgeId.reset();
                }
                removed = true;
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
            if (m_isSpacePressed) {
                GdkDisplay* display = gdk_window_get_display(win);
                GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND1);
                gdk_window_set_cursor(win, cursor);
                if (cursor)
                    g_object_unref(cursor);
            } else {
                gdk_window_set_cursor(win, nullptr);
            }
        }
        return TRUE;
    }

    if (m_isDraggingCard) {
        if (m_activeSnapType == FluidCore::SnapType::StackMerge && !m_activeMergeTargetId.empty()) {
            m_api.mergeNodesIntoStack(m_dragCandidateNodeId, m_activeMergeTargetId);
        }

        m_isDraggingCard = false;
        m_dragPending = false;
        m_dragCandidateNodeId.clear();
        m_activeMergeTargetId.clear();
        m_activeSnapGuideLines.clear();
        m_activeSnapType = FluidCore::SnapType::None;

        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            gdk_window_set_cursor(win, nullptr);
        }

        gtk_widget_queue_draw(m_area);
        return TRUE;
    }

    m_dragPending = false;

    if (m_isConnecting) {
        m_isConnecting = false;
        FluidCore::Point wPt = screenToWorld(event->x, event->y);
        const auto* targetNode = hitTestNodeAtWorldPoint(wPt);
        if (targetNode && targetNode->id() != m_connectorSourceNodeId) {
            FluidCore::Color edgeColor{static_cast<unsigned char>((m_currentColor >> 16) & 0xFF),
                                       static_cast<unsigned char>((m_currentColor >> 8) & 0xFF),
                                       static_cast<unsigned char>(m_currentColor & 0xFF), 255};
            if (m_currentColor == 0x000000) {
                edgeColor = {30, 144, 255, 255}; // Default clean DodgerBlue for connectors
            }
            m_api.createInkLink(m_connectorSourceNodeId, targetNode->id(), edgeColor);
        }
        m_connectorSourceNodeId.clear();
        m_connectorTargetHoverNodeId.clear();
        gtk_widget_queue_draw(m_area);
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
                m_activeStroke.points.push_back(
                    FluidCore::XoppPoint{sample.point.x, sample.point.y});
            }

            if (!m_activeStroke.points.empty()) {
                bool convertedToConnector = false;

                // Safe classification check (only for "pen" tool with >= 2 points)
                if (m_currentTool == "pen" && m_activeStroke.points.size() >= 2) {
                    const auto& ptStart = m_activeStroke.points.front();
                    const auto& ptEnd = m_activeStroke.points.back();
                    const auto* srcNode =
                        hitTestNodeAtWorldPoint(FluidCore::Point{ptStart.x, ptStart.y});
                    const auto* dstNode =
                        hitTestNodeAtWorldPoint(FluidCore::Point{ptEnd.x, ptEnd.y});

                    // Must start on node A and end on distinct node B (self-loops rejected)
                    if (srcNode && dstNode && srcNode->id() != dstNode->id()) {
                        // Compute direct chord distance vs total arc length
                        const double dx = ptEnd.x - ptStart.x;
                        const double dy = ptEnd.y - ptStart.y;
                        const double chordDist = std::sqrt(dx * dx + dy * dy);

                        double totalArcLen = 0.0;
                        for (size_t i = 1; i < m_activeStroke.points.size(); ++i) {
                            const double segDx =
                                m_activeStroke.points[i].x - m_activeStroke.points[i - 1].x;
                            const double segDy =
                                m_activeStroke.points[i].y - m_activeStroke.points[i - 1].y;
                            totalArcLen += std::sqrt(segDx * segDx + segDy * segDy);
                        }

                        const double straightness =
                            totalArcLen > 1e-6 ? (chordDist / totalArcLen) : 1.0;

                        // Strict straightness threshold (>= 0.82) to avoid converting
                        // circles/underlines
                        if (straightness >= 0.82) {
                            FluidCore::Color edgeColor{
                                static_cast<unsigned char>((m_currentColor >> 16) & 0xFF),
                                static_cast<unsigned char>((m_currentColor >> 8) & 0xFF),
                                static_cast<unsigned char>(m_currentColor & 0xFF), 255};
                            if (m_currentColor == 0x000000) {
                                edgeColor = {30, 144, 255, 255};
                            }
                            m_api.createInkLink(srcNode->id(), dstNode->id(), edgeColor);
                            convertedToConnector = true;
                        }
                    }
                }

                if (!convertedToConnector) {
                    m_api.insertNode(std::make_unique<FluidCore::CanvasStrokeNode>(m_activeStroke));
                }
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

    // Handle Drag initiation threshold (6.0 screen px)
    if (m_dragPending && !m_isDraggingCard) {
        const double dragDist =
            std::hypot(event->x - m_dragStartScreenX, event->y - m_dragStartScreenY);
        if (dragDist >= 6.0) {
            m_isDraggingCard = true;
            if (m_dragCandidateIsChild && !m_dragCandidateParentStackId.empty()) {
                m_api.extractChildFromStack(m_dragCandidateParentStackId, m_dragCandidateNodeId,
                                            m_dragInitialWorldPos);
                m_dragCandidateIsChild = false;
                m_dragCandidateParentStackId.clear();
            }
        }
    }

    // Active Card & Stack Dragging with 16pt Magnetic Snapping & Stack Merge
    if (m_isDraggingCard && !m_dragCandidateNodeId.empty()) {
        const FluidCore::Point currentWorld = screenToWorld(event->x, event->y);
        const FluidCore::Rectangle nodeBounds = m_api.getNodeBounds(m_dragCandidateNodeId);
        const FluidCore::Rectangle proposedBounds{currentWorld.x - m_dragOffsetWorld.x,
                                                  currentWorld.y - m_dragOffsetWorld.y,
                                                  nodeBounds.w, nodeBounds.h};

        const double snapThresholdWorld = 16.0 / m_zoom;
        const FluidCore::SnapResult snapRes =
            m_api.solveSnap(proposedBounds, snapThresholdWorld, m_dragCandidateNodeId);

        if (snapRes.type == FluidCore::SnapType::StackMerge) {
            m_activeSnapType = FluidCore::SnapType::StackMerge;
            m_activeMergeTargetId = snapRes.targetNodeId;
            m_activeSnapGuideLines.clear();
            m_draggedGhostBounds = proposedBounds;
            m_api.updateNodePosition(m_dragCandidateNodeId, proposedBounds.x, proposedBounds.y);
        } else if (snapRes.type == FluidCore::SnapType::MagneticSnap) {
            m_activeSnapType = FluidCore::SnapType::MagneticSnap;
            m_activeMergeTargetId.clear();
            m_activeSnapGuideLines = snapRes.guideLines;
            m_draggedGhostBounds = snapRes.snappedBounds;
            m_api.updateNodePosition(m_dragCandidateNodeId, snapRes.snappedBounds.x,
                                     snapRes.snappedBounds.y);
        } else {
            m_activeSnapType = FluidCore::SnapType::None;
            m_activeMergeTargetId.clear();
            m_activeSnapGuideLines.clear();
            m_draggedGhostBounds = proposedBounds;
            m_api.updateNodePosition(m_dragCandidateNodeId, proposedBounds.x, proposedBounds.y);
        }

        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            GdkDisplay* display = gdk_window_get_display(win);
            GdkCursor* grabCursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
            gdk_window_set_cursor(win, grabCursor);
            if (grabCursor)
                g_object_unref(grabCursor);
        }

        gtk_widget_queue_draw(m_area);
        return TRUE;
    }

    if (m_isConnecting) {
        m_connectorCurrentWorld = screenToWorld(event->x, event->y);
        const auto* targetNode = hitTestNodeAtWorldPoint(m_connectorCurrentWorld);
        m_connectorTargetHoverNodeId =
            (targetNode && targetNode->id() != m_connectorSourceNodeId) ? targetNode->id() : "";
        gtk_widget_queue_draw(m_area);
        return TRUE;
    }

    if (m_isDrawing) {
        FluidCore::Point wPt = screenToWorld(event->x, event->y);

        if (m_currentTool == "pen" || m_currentTool == "highlighter") {
            auto result = m_stabilizer.pushPoint(
                FluidCoreApp::StrokeStabilizer::Point2D{wPt.x, wPt.y}, 1.0, g_get_real_time());
            for (const auto& seg : result.newlyCommitted) {
                m_activeSegments.push_back(seg);
            }
            m_hasWetSegment = result.hasWetSegment;
            m_activeWetTip = result.wetTip;
            gtk_widget_queue_draw(m_area);
        } else if (m_currentTool == "eraser") {
            const double wRadius = 30.0 / m_zoom; // 30 screen pixels
            const FluidCore::Rectangle queryRect{wPt.x - wRadius, wPt.y - wRadius, wRadius * 2.0,
                                                 wRadius * 2.0};

            auto hits = m_api.queryVisibleNodes(queryRect);
            bool removed = false;
            for (const auto* hit : hits) {
                if (const auto* strokeNode =
                        dynamic_cast<const FluidCore::CanvasStrokeNode*>(hit)) {
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

        // Check if mouse is hovering over an anchor pill or stack chevron
        std::string newHoveredId;
        bool isHoveringChevron = false;
        if (m_zoom >= 0.2) {
            const FluidCore::Point worldTopLeft = screenToWorld(0, 0);
            const FluidCore::Point worldBottomRight = screenToWorld(
                alloc.width > 0 ? alloc.width : 800, alloc.height > 0 ? alloc.height : 600);
            const FluidCore::Rectangle viewWorldRect{
                worldTopLeft.x, worldTopLeft.y, std::max(0.0, worldBottomRight.x - worldTopLeft.x),
                std::max(0.0, worldBottomRight.y - worldTopLeft.y)};

            auto visibleNodes = m_api.queryVisibleNodes(viewWorldRect);
            for (const auto* node : visibleNodes) {
                if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                    const auto chevRect = getStackChevronRect(stack);
                    if (event->x >= chevRect.x && event->x <= chevRect.x + chevRect.w &&
                        event->y >= chevRect.y && event->y <= chevRect.y + chevRect.h) {
                        isHoveringChevron = true;
                        break;
                    }
                }

                if (dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
                    const auto pillRect = getExcerptAnchorPillRect(node);
                    if (event->x >= pillRect.x && event->x <= pillRect.x + pillRect.w &&
                        event->y >= pillRect.y && event->y <= pillRect.y + pillRect.h) {
                        newHoveredId = node->id();
                        break;
                    }
                }
            }
        }

        if (newHoveredId != m_hoveredAnchorCardId) {
            m_hoveredAnchorCardId = newHoveredId;
            gtk_widget_queue_draw(m_area);
        }

        if (!m_hoveredAnchorCardId.empty() || isHoveringChevron) {
            GdkCursor* pointerCursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(win, pointerCursor);
            if (pointerCursor)
                g_object_unref(pointerCursor);
        } else if (minimapHitTest(event->x, event->y, alloc.width, alloc.height)) {
            GdkCursor* pointerCursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(win, pointerCursor);
            if (pointerCursor)
                g_object_unref(pointerCursor);
        } else if (m_currentTool != "pen" && m_currentTool != "highlighter" &&
                   m_currentTool != "eraser" && m_currentTool != "connector") {
            const FluidCore::Point wPt = screenToWorld(event->x, event->y);
            const auto* hitNode = hitTestNodeAtWorldPoint(wPt);
            if (hitNode) {
                GdkCursor* moveCursor = gdk_cursor_new_for_display(display, GDK_HAND1);
                gdk_window_set_cursor(win, moveCursor);
                if (moveCursor)
                    g_object_unref(moveCursor);
            } else {
                gdk_window_set_cursor(win, nullptr);
            }
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
    case GDK_KEY_space:
        setSpacePressed(true);
        return TRUE;
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
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_BackSpace: {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            GtkWidget* focusWidget =
                gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(m_area)));
            if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
                return FALSE;
            }
        }

        if (m_selectedEdgeId.has_value()) {
            m_api.removeEdge(*m_selectedEdgeId);
            m_selectedEdgeId.reset();
            gtk_widget_queue_draw(m_area);
            return TRUE;
        }

        if (m_selectedNodeId.has_value()) {
            m_api.removeNode(*m_selectedNodeId);
            m_selectedNodeId.reset();
            gtk_widget_queue_draw(m_area);
            return TRUE;
        }
        break;
    }
    default:
        break;
    }
    return FALSE;
}

gboolean WorkspaceView::onKeyRelease(GdkEventKey* event) {
    if (event->keyval == GDK_KEY_space) {
        setSpacePressed(false);
        return TRUE;
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
    const double radius = 8.0 * m_zoom;

    // 1. Soft layered elevation card shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.04);
    drawRoundedRect(cr, sx, sy + 3.0 * m_zoom, sw, sh, radius);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx, sy + 1.0 * m_zoom, sw, sh, radius);
    cairo_fill(cr);

    // 2. Card background container
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);

    // 3. Card border
    cairo_set_source_rgba(cr, 0.82, 0.86, 0.92, 0.95);
    cairo_set_line_width(cr, std::max(1.0, 1.0 * m_zoom));
    cairo_stroke(cr);

    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_clip(cr);

    const double headerH = std::min(28.0 * m_zoom, sh * 0.35);

    // 4. Header background bar
    cairo_rectangle(cr, sx, sy, sw, headerH);
    cairo_set_source_rgb(cr, 0.965, 0.975, 0.99);
    cairo_fill(cr);

    // Header divider line
    cairo_move_to(cr, sx, sy + headerH);
    cairo_line_to(cr, sx + sw, sy + headerH);
    cairo_set_source_rgba(cr, 0.88, 0.91, 0.95, 0.9);
    cairo_set_line_width(cr, std::max(0.75, 0.85 * m_zoom));
    cairo_stroke(cr);

    // 5. Left accent indicator bar
    if (excerpt) {
        const auto col = excerpt->color();
        cairo_set_source_rgba(cr, col.r / 255.0, col.g / 255.0, col.b / 255.0, 0.95);
        const double barW = std::max(3.0, 4.5 * m_zoom);
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
        cairo_set_font_size(cr, 10.5 * m_zoom);
        cairo_set_source_rgb(cr, 0.22, 0.28, 0.38);
        cairo_move_to(cr, sx + 14.0 * m_zoom, sy + headerH * 0.67);
        cairo_show_text(cr, headerStr.c_str());

        // 7. Return Anchor Pill on header right
        if (excerpt) {
            const double pillW = 72.0 * m_zoom;
            const double pillH = 20.0 * m_zoom;
            const double pillX = sx + sw - pillW - 8.0 * m_zoom;
            const double pillY = sy + (headerH - pillH) / 2.0;
            const bool isHovered = (excerpt->id() == m_hoveredAnchorCardId);

            if (pillW > 20.0) {
                drawRoundedRect(cr, pillX, pillY, pillW, pillH, pillH / 2.0);
                if (isHovered) {
                    cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, 0.20);
                } else {
                    cairo_set_source_rgba(cr, 0.05, 0.45, 0.90, 0.08);
                }
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.05, 0.50, 0.95, isHovered ? 0.80 : 0.40);
                cairo_set_line_width(cr, isHovered ? 1.5 * m_zoom : 1.0 * m_zoom);
                cairo_stroke(cr);

                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, 8.5 * m_zoom);
                if (isHovered) {
                    cairo_set_source_rgb(cr, 0.01, 0.30, 0.85);
                } else {
                    cairo_set_source_rgb(cr, 0.05, 0.40, 0.85);
                }
                cairo_text_extents_t ext;
                cairo_text_extents(cr, "↗ Anchor", &ext);
                cairo_move_to(cr, pillX + (pillW - ext.width) / 2.0,
                              pillY + (pillH + ext.height) / 2.0 - 0.5 * m_zoom);
                cairo_show_text(cr, "↗ Anchor");
            }
        }
    }

    // Render active return focus flash aura
    if (m_flashAlpha > 0.01 && excerpt && excerpt->id() == m_flashCardId) {
        cairo_save(cr);
        drawRoundedRect(cr, sx - 4.0, sy - 4.0, sw + 8.0, sh + 8.0, radius + 4.0);
        cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.35 * m_flashAlpha);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.90 * m_flashAlpha);
        cairo_set_line_width(cr, 2.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // 8. Body Content Rendering
    if (excerpt) {
        if (excerpt->isImageExcerpt()) {
            const double bodyX = sx + 10.0 * m_zoom;
            const double bodyY = sy + headerH + 6.0 * m_zoom;
            const double bodyW = sw - 20.0 * m_zoom;
            const double bodyH = sh - headerH - 12.0 * m_zoom;

            if (bodyW > 8.0 && bodyH > 8.0) {
                CairoSurfaceHandle surface;
                if (m_excerptTileCache) {
                    LodTier tier = computeLodTierFromZoom(m_zoom);
                    CropCacheKey key = CropCacheKey::fromNormalizedRect(
                        excerpt->sourceDocId(), excerpt->sourcePageNo(),
                        excerpt->sourceNormalizedRect(), tier);
                    surface = m_excerptTileCache->get(key);
                    if (!surface) {
                        surface = m_excerptTileCache->getBestAvailableSurface(
                            excerpt->sourceDocId(), excerpt->sourcePageNo(),
                            excerpt->sourceNormalizedRect());
                        // Dispatch async request for target LoD tier
                        m_excerptTileCache->requestCropAsync(
                            excerpt->id(), excerpt->sourceDocId(), excerpt->sourcePageNo(),
                            excerpt->sourceNormalizedRect(), excerpt->bounds().w - 20.0,
                            excerpt->bounds().h - 36.0, m_zoom);
                    }
                }

                if (surface) {
                    drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                    cairo_save(cr);
                    cairo_clip(cr);

                    // Solid background container
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

                    // Crisp container border
                    drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                    cairo_set_source_rgba(cr, 0.75, 0.82, 0.90, 0.9);
                    cairo_set_line_width(cr, 1.0);
                    cairo_stroke(cr);
                } else {
                    // Fallback / Loading glassmorphic placeholder
                    drawRoundedRect(cr, bodyX, bodyY, bodyW, bodyH, 4.0);
                    cairo_set_source_rgb(cr, 0.94, 0.96, 0.99);
                    cairo_fill_preserve(cr);

                    cairo_set_source_rgb(cr, 0.75, 0.80, 0.88);
                    double dashes[] = {4.0, 4.0};
                    cairo_set_dash(cr, dashes, 2, 0.0);
                    cairo_set_line_width(cr, 1.0);
                    cairo_stroke(cr);
                    cairo_set_dash(cr, nullptr, 0, 0.0);

                    if (m_zoom >= 0.25) {
                        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                               CAIRO_FONT_WEIGHT_BOLD);
                        cairo_set_font_size(cr, 10.0 * m_zoom);
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
            if (m_zoom < 0.25) {
                // Micro zoom: render abstracted overview paragraph bars
                const double barStartX = sx + 14.0 * m_zoom;
                const double barW = sw - 28.0 * m_zoom;
                double curY = sy + headerH + 6.0 * m_zoom;
                cairo_set_source_rgba(cr, 0.70, 0.75, 0.83, 0.7);
                for (int i = 0; i < 4 && curY + 4.0 * m_zoom < sy + sh - 4.0 * m_zoom; ++i) {
                    double wFraction = (i == 3) ? 0.6 : ((i % 2 == 0) ? 0.95 : 0.85);
                    cairo_rectangle(cr, barStartX, curY, barW * wFraction, 2.5 * m_zoom);
                    cairo_fill(cr);
                    curY += 5.0 * m_zoom;
                }
            } else {
                const double textStartX = sx + 16.0 * m_zoom;
                const double maxW = sw - 32.0 * m_zoom;
                const double fontSize = 11.5 * m_zoom;
                const double lineSpacing = 16.5 * m_zoom;
                double curY = sy + headerH + 18.0 * m_zoom;

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

void WorkspaceView::drawGenericNode(cairo_t* cr, const FluidCore::WorkspaceNode* node, double sx,
                                    double sy, double sw, double sh) {
    const double radius = 8.0 * m_zoom;

    // Card shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.04);
    drawRoundedRect(cr, sx, sy + 3.0 * m_zoom, sw, sh, radius);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx, sy + 1.0 * m_zoom, sw, sh, radius);
    cairo_fill(cr);

    // Card background
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.82, 0.86, 0.92, 0.95);
    cairo_set_line_width(cr, std::max(1.0, 1.0 * m_zoom));
    cairo_stroke(cr);

    // Top accent bar
    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_clip(cr);
    cairo_rectangle(cr, sx, sy, sw, 5.0 * m_zoom);
    cairo_set_source_rgb(cr, 0.20, 0.55, 0.90);
    cairo_fill(cr);

    // Title / Label
    if (m_zoom >= 0.25) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0 * m_zoom);
        cairo_set_source_rgb(cr, 0.20, 0.26, 0.35);
        cairo_move_to(cr, sx + 14.0 * m_zoom, sy + 24.0 * m_zoom);
        cairo_show_text(cr, node->id().c_str());
    }
    cairo_restore(cr);
}

void WorkspaceView::drawCardStack(cairo_t* cr, const FluidCore::WorkspaceNode* node, double sx,
                                  double sy, double sw, double sh) {
    const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node);
    if (!stack) {
        return;
    }

    const double radius = 8.0 * m_zoom;
    const double headerH = FluidCore::CardStackNode::kHeaderHeight * m_zoom;
    const bool isCollapsed = stack->isCollapsed();
    const bool isSelected = (m_selectedNodeId && *m_selectedNodeId == stack->id());

    // 1. Layered Deck Drop Shadows
    if (isCollapsed) {
        // Multi-card silhouette tabs peeking from behind
        for (int i = 2; i >= 1; --i) {
            const double offset = static_cast<double>(i) * 3.5 * m_zoom;
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

    // Main Stack Container Shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.06);
    drawRoundedRect(cr, sx, sy + 4.0 * m_zoom, sw, sh, radius);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.08);
    drawRoundedRect(cr, sx, sy + 1.5 * m_zoom, sw, sh, radius);
    cairo_fill(cr);

    // Selection Halo
    if (isSelected) {
        cairo_save(cr);
        drawRoundedRect(cr, sx - 3.0, sy - 3.0, sw + 6.0, sh + 6.0, radius + 2.0);
        cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.35);
        cairo_set_line_width(cr, 2.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Main Container Background
    drawRoundedRect(cr, sx, sy, sw, sh, radius);
    cairo_set_source_rgb(cr, 0.985, 0.988, 0.995);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.76, 0.82, 0.90, 0.95);
    cairo_set_line_width(cr, std::max(1.0, 1.2 * m_zoom));
    cairo_stroke(cr);

    // 2. Render Children (when expanded)
    if (!isCollapsed) {
        for (const auto& child : stack->children()) {
            if (!child)
                continue;
            const auto cb = child->bounds();
            const double csx = (cb.x - m_originX) * m_zoom;
            const double csy = (cb.y - m_originY) * m_zoom;
            const double csw = cb.w * m_zoom;
            const double csh = cb.h * m_zoom;

            if (dynamic_cast<const FluidCore::ExcerptCardNode*>(child.get())) {
                drawExcerptCard(cr, child.get(), csx, csy, csw, csh);
            } else if (dynamic_cast<const FluidCore::CardStackNode*>(child.get())) {
                drawCardStack(cr, child.get(), csx, csy, csw, csh);
            } else {
                drawGenericNode(cr, child.get(), csx, csy, csw, csh);
            }
        }
    }

    // 3. Stack Header Bar
    cairo_save(cr);
    drawRoundedRect(cr, sx, sy, sw, headerH, radius);
    cairo_clip(cr);

    // Header gradient (slate-800 to slate-900)
    cairo_pattern_t* grad = cairo_pattern_create_linear(sx, sy, sx, sy + headerH);
    cairo_pattern_add_color_stop_rgb(grad, 0.0, 0.16, 0.20, 0.28);
    cairo_pattern_add_color_stop_rgb(grad, 1.0, 0.11, 0.14, 0.20);
    cairo_set_source(cr, grad);
    cairo_rectangle(cr, sx, sy, sw, headerH);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    // Header bottom divider line
    cairo_move_to(cr, sx, sy + headerH);
    cairo_line_to(cr, sx + sw, sy + headerH);
    cairo_set_source_rgba(cr, 0.28, 0.35, 0.48, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Chevron Button ([▼] or [▶])
    const double chevronSize = std::min(18.0 * m_zoom, headerH * 0.7);
    const double chevronX = sx + 8.0 * m_zoom;
    const double chevronY = sy + (headerH - chevronSize) / 2.0;

    drawRoundedRect(cr, chevronX, chevronY, chevronSize, chevronSize, chevronSize / 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.90, 0.94, 1.0);
    if (!isCollapsed) {
        // Downward triangle
        const double cx = chevronX + chevronSize / 2.0;
        const double cy = chevronY + chevronSize / 2.0;
        const double triR = chevronSize * 0.25;
        cairo_move_to(cr, cx - triR, cy - triR * 0.6);
        cairo_line_to(cr, cx + triR, cy - triR * 0.6);
        cairo_line_to(cr, cx, cy + triR * 0.8);
        cairo_close_path(cr);
        cairo_fill(cr);
    } else {
        // Rightward triangle
        const double cx = chevronX + chevronSize / 2.0;
        const double cy = chevronY + chevronSize / 2.0;
        const double triR = chevronSize * 0.25;
        cairo_move_to(cr, cx - triR * 0.6, cy - triR);
        cairo_line_to(cr, cx + triR * 0.8, cy);
        cairo_line_to(cr, cx - triR * 0.6, cy + triR);
        cairo_close_path(cr);
        cairo_fill(cr);
    }

    if (m_zoom >= 0.2) {
        // Stack Title
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0 * m_zoom);
        cairo_set_source_rgb(cr, 0.95, 0.97, 1.0);
        cairo_move_to(cr, chevronX + chevronSize + 8.0 * m_zoom, sy + headerH * 0.65);
        cairo_show_text(cr, stack->title().c_str());

        // Count Pill Badge on header right
        std::string countStr = std::to_string(stack->childCount()) + " cards";
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9.0 * m_zoom);
        cairo_text_extents_t cntExt;
        cairo_text_extents(cr, countStr.c_str(), &cntExt);

        const double badgeW = cntExt.width + 12.0 * m_zoom;
        const double badgeH = 18.0 * m_zoom;
        const double badgeX = sx + sw - badgeW - 8.0 * m_zoom;
        const double badgeY = sy + (headerH - badgeH) / 2.0;

        if (badgeW > 10.0 && badgeX > chevronX + chevronSize + 40.0 * m_zoom) {
            drawRoundedRect(cr, badgeX, badgeY, badgeW, badgeH, badgeH / 2.0);
            cairo_set_source_rgba(cr, 0.05, 0.55, 0.95, 0.28);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.20, 0.70, 1.0, 0.60);
            cairo_set_line_width(cr, 0.8 * m_zoom);
            cairo_stroke(cr);

            cairo_set_source_rgb(cr, 0.75, 0.90, 1.0);
            cairo_move_to(cr, badgeX + (badgeW - cntExt.width) / 2.0,
                          badgeY + (badgeH + cntExt.height) / 2.0 - 0.5 * m_zoom);
            cairo_show_text(cr, countStr.c_str());
        }
    }

    cairo_restore(cr);
}

void WorkspaceView::drawMagneticSnapGuides(cairo_t* cr) {
    if (m_activeSnapGuideLines.empty()) {
        return;
    }

    cairo_save(cr);
    for (const auto& g : m_activeSnapGuideLines) {
        const FluidCore::Point s1 = worldToScreen(g.start.x, g.start.y);
        const FluidCore::Point s2 = worldToScreen(g.end.x, g.end.y);

        // Ambient glow pass
        cairo_set_source_rgba(cr, 0.0, 0.82, 1.0, 0.30);
        cairo_set_line_width(cr, 4.0);
        cairo_move_to(cr, s1.x, s1.y);
        cairo_line_to(cr, s2.x, s2.y);
        cairo_stroke(cr);

        // Core dashed snap guideline
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

void WorkspaceView::drawStackMergeGhost(cairo_t* cr) {
    if (m_activeSnapType != FluidCore::SnapType::StackMerge || m_activeMergeTargetId.empty()) {
        return;
    }

    FluidCore::Rectangle targetBounds = m_api.getNodeBounds(m_activeMergeTargetId);
    if (targetBounds.w <= 0.0 || targetBounds.h <= 0.0) {
        return;
    }

    const FluidCore::Point sp = worldToScreen(targetBounds.x, targetBounds.y);
    const double sw = targetBounds.w * m_zoom;
    const double sh = targetBounds.h * m_zoom;
    const double radius = 10.0 * m_zoom;

    cairo_save(cr);

    // Glowing docking aura
    drawRoundedRect(cr, sp.x - 4.0, sp.y - 4.0, sw + 8.0, sh + 8.0, radius);
    cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.18);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.05, 0.70, 1.0, 0.90);
    double dashes[] = {6.0, 4.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_set_line_width(cr, 2.5);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0.0);

    // Central "+ Drop to Stack" badge
    const double badgeW = std::min(130.0 * m_zoom, sw * 0.8);
    const double badgeH = 28.0 * m_zoom;
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
        cairo_set_font_size(cr, 11.0 * m_zoom);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, "+ Drop to Stack", &ext);
        cairo_move_to(cr, badgeX + (badgeW - ext.width) / 2.0,
                      badgeY + (badgeH + ext.height) / 2.0 - 0.5 * m_zoom);
        cairo_show_text(cr, "+ Drop to Stack");
    }

    cairo_restore(cr);
}

const FluidCore::WorkspaceNode*
WorkspaceView::hitTestChildNodeAtWorldPoint(const FluidCore::Point& worldPt,
                                            std::string* outParentStackId) const {
    const FluidCore::Rectangle queryRect{worldPt.x - 1.0, worldPt.y - 1.0, 2.0, 2.0};
    auto hits = m_api.queryVisibleNodes(queryRect);
    for (const auto* node : hits) {
        if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            if (!stack->isCollapsed()) {
                const auto& children = stack->children();
                for (auto it = children.rbegin(); it != children.rend(); ++it) {
                    if (*it) {
                        const auto b = (*it)->bounds();
                        if (worldPt.x >= b.x && worldPt.x <= b.x + b.w && worldPt.y >= b.y &&
                            worldPt.y <= b.y + b.h) {
                            if (outParentStackId) {
                                *outParentStackId = stack->id();
                            }
                            return it->get();
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

const FluidCore::WorkspaceNode*
WorkspaceView::hitTestNodeAtWorldPoint(const FluidCore::Point& worldPt) const {
    const FluidCore::Rectangle queryRect{worldPt.x - 1.0, worldPt.y - 1.0, 2.0, 2.0};
    auto hits = m_api.queryVisibleNodes(queryRect);
    for (const auto* node : hits) {
        if (dynamic_cast<const FluidCore::ExcerptCardNode*>(node) ||
            dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            const auto b = node->bounds();
            if (worldPt.x >= b.x && worldPt.x <= b.x + b.w && worldPt.y >= b.y &&
                worldPt.y <= b.y + b.h) {
                return node;
            }
        }
    }
    for (const auto* node : hits) {
        if (!dynamic_cast<const FluidCore::CanvasStrokeNode*>(node)) {
            const auto b = node->bounds();
            if (worldPt.x >= b.x && worldPt.x <= b.x + b.w && worldPt.y >= b.y &&
                worldPt.y <= b.y + b.h) {
                return node;
            }
        }
    }
    return nullptr;
}

std::string WorkspaceView::hitTestEdgeAtWorldPoint(const FluidCore::Point& worldPt,
                                                   double tolerance) const {
    const auto edgeIds = m_api.getAllEdges();
    for (const auto& eid : edgeIds) {
        FluidCore::BezierSpline spline = m_api.getEdgeGeometry(eid);
        if (FluidCore::GraphTopology::hitTestSpline(spline, worldPt, tolerance)) {
            return eid;
        }
    }
    return "";
}

void WorkspaceView::drawArrowHead(cairo_t* cr, const FluidCore::Point& tip, double angle,
                                  double size, uint32_t color) {
    const double arrowAngle = M_PI / 6.0; // 30 degrees
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

void WorkspaceView::drawGraphEdges(cairo_t* cr) {
    auto* engine = dynamic_cast<FluidCore::FluidCoreEngine*>(&m_api);
    std::vector<std::string> allEdges = m_api.getAllEdges();
    for (const auto& edgeId : allEdges) {
        FluidCore::BezierSpline spline = m_api.getEdgeGeometry(edgeId);
        if (spline.controlPoints.size() < 4) {
            continue;
        }

        const auto& p0W = spline.controlPoints[0];
        const auto& p1W = spline.controlPoints[1];
        const auto& p2W = spline.controlPoints[2];
        const auto& p3W = spline.controlPoints[3];

        const FluidCore::Point s0 = worldToScreen(p0W.x, p0W.y);
        const FluidCore::Point s1 = worldToScreen(p1W.x, p1W.y);
        const FluidCore::Point s2 = worldToScreen(p2W.x, p2W.y);
        const FluidCore::Point s3 = worldToScreen(p3W.x, p3W.y);

        const bool isSelected = (m_selectedEdgeId && *m_selectedEdgeId == edgeId);

        // Render selection glow halo if selected
        if (isSelected) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 0.05, 0.65, 1.0, 0.35);
            cairo_set_line_width(cr, std::max(6.0, 8.0 * m_zoom));
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_move_to(cr, s0.x, s0.y);
            cairo_curve_to(cr, s1.x, s1.y, s2.x, s2.y, s3.x, s3.y);
            cairo_stroke(cr);
            cairo_restore(cr);
        }

        // Render shadow under connector curve
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.08);
        cairo_set_line_width(cr, std::max(2.5, 3.5 * m_zoom));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, s0.x, s0.y + 1.5 * m_zoom);
        cairo_curve_to(cr, s1.x, s1.y + 1.5 * m_zoom, s2.x, s2.y + 1.5 * m_zoom, s3.x,
                       s3.y + 1.5 * m_zoom);
        cairo_stroke(cr);
        cairo_restore(cr);

        // Render main connector curve
        cairo_save(cr);
        if (isSelected) {
            cairo_set_source_rgb(cr, 0.02, 0.45, 0.90);
            cairo_set_line_width(cr, std::max(2.2, 3.0 * m_zoom));
        } else {
            cairo_set_source_rgb(cr, 0.12, 0.50, 0.95);
            cairo_set_line_width(cr, std::max(1.5, 2.2 * m_zoom));
        }
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, s0.x, s0.y);
        cairo_curve_to(cr, s1.x, s1.y, s2.x, s2.y, s3.x, s3.y);
        cairo_stroke(cr);

        // Check edge direction for arrowhead rendering
        bool isBidirectional = false;
        if (engine) {
            auto edgeOpt = engine->graphTopology().findEdge(edgeId);
            if (edgeOpt && edgeOpt->direction == FluidCore::EdgeDirection::Bidirectional) {
                isBidirectional = true;
            }
        }

        const double arrowSize = std::clamp(12.0 * m_zoom, 7.0, 20.0);

        // Forward arrowhead at target node (s3)
        double tangentTargetX = s3.x - s2.x;
        double tangentTargetY = s3.y - s2.y;
        if (std::abs(tangentTargetX) < 1e-6 && std::abs(tangentTargetY) < 1e-6) {
            tangentTargetX = s3.x - s0.x;
            tangentTargetY = s3.y - s0.y;
        }
        const double arrivalAngle = std::atan2(tangentTargetY, tangentTargetX);
        drawArrowHead(cr, s3, arrivalAngle, arrowSize, isSelected ? 0x0374B5 : 0x1E88E5);

        // Bidirectional backward arrowhead at source node (s0)
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

    // If active connector tool drag in progress, render live rubber-band preview
    if (m_isConnecting) {
        const FluidCore::Point sStart =
            worldToScreen(m_connectorStartWorld.x, m_connectorStartWorld.y);
        const FluidCore::Point sCur =
            worldToScreen(m_connectorCurrentWorld.x, m_connectorCurrentWorld.y);

        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.12, 0.55, 0.95, 0.85);
        double dashes[] = {6.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_set_line_width(cr, std::max(1.8, 2.5 * m_zoom));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, sStart.x, sStart.y);
        cairo_line_to(cr, sCur.x, sCur.y);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0.0);

        const double angle = std::atan2(sCur.y - sStart.y, sCur.x - sStart.x);
        const double arrowSize = std::clamp(12.0 * m_zoom, 7.0, 20.0);
        drawArrowHead(cr, sCur, angle, arrowSize, 0x1E88E5);
        cairo_restore(cr);
    }
}

void WorkspaceView::draw(cairo_t* cr, int width, int height) {
    // Clear background canvas with modern slate-50 tone
    cairo_set_source_rgb(cr, 0.975, 0.982, 0.990);
    cairo_paint(cr);

    // Render infinite dot-grid
    drawBackgroundGrid(cr, width, height);

    // Render reactive relational graph edges & dynamic cubic Bézier splines
    drawGraphEdges(cr);

    // Viewport spatial culling query (O(log N) R-tree query with 150pt safety padding)
    const double pad = 150.0 / m_zoom;
    const FluidCore::Rectangle viewport{m_originX - pad, m_originY - pad,
                                        width / m_zoom + 2.0 * pad, height / m_zoom + 2.0 * pad};
    const std::vector<FluidCore::WorkspaceNode*> visibleNodes = m_api.queryVisibleNodes(viewport);

    for (const FluidCore::WorkspaceNode* node : visibleNodes) {
        if (dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - m_originX) * m_zoom;
            const double sy = (b.y - m_originY) * m_zoom;
            const double sw = b.w * m_zoom;
            const double sh = b.h * m_zoom;
            drawCardStack(cr, node, sx, sy, sw, sh);
        } else if (dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
            const FluidCore::Rectangle b = node->bounds();
            const double sx = (b.x - m_originX) * m_zoom;
            const double sy = (b.y - m_originY) * m_zoom;
            const double sw = b.w * m_zoom;
            const double sh = b.h * m_zoom;
            drawExcerptCard(cr, node, sx, sy, sw, sh);
        } else if (const auto* strokeNode =
                       dynamic_cast<const FluidCore::CanvasStrokeNode*>(node)) {
            const auto& stroke = strokeNode->stroke();
            if (stroke.points.empty())
                continue;

            cairo_set_source_rgba(
                cr, ((stroke.color >> 16) & 0xFF) / 255.0, ((stroke.color >> 8) & 0xFF) / 255.0,
                (stroke.color & 0xFF) / 255.0, stroke.tool == "highlighter" ? 0.45 : 1.0);
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
        cairo_set_source_rgba(
            cr, ((m_currentColor >> 16) & 0xFF) / 255.0, ((m_currentColor >> 8) & 0xFF) / 255.0,
            (m_currentColor & 0xFF) / 255.0, m_currentTool == "highlighter" ? 0.45 : 1.0);
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

    // Render 16pt magnetic snapping guidelines during drag
    drawMagneticSnapGuides(cr);

    // Render stack-merge docking aura and ghost during drag
    drawStackMergeGhost(cr);

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
