#include "workspace/WorkspaceView.h"
#include "FluidCoreEngine.h"
#include "graph/GraphTopology.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/CardLayoutEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/ExcerptPayload.h"
#include "workspace/WorkspaceInteraction.h"
#include "workspace/WorkspaceRenderer.h"

#include <algorithm>
#include <cmath>

namespace FluidCoreApp {

namespace {
constexpr double kMinZoom = 0.10; // 10%
constexpr double kMaxZoom = 2.0;  // 200%
} // namespace

WorkspaceView::WorkspaceView(FluidCore::FluidCoreAPI& api) : m_api(api) {
    m_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(m_area, TRUE);
    gtk_widget_add_events(m_area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                      GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
                                      GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
                                      GDK_SMOOTH_SCROLL_MASK);

    g_object_set_data(G_OBJECT(m_area), "workspace-view-instance", this);

    g_object_set_data_full(
        G_OBJECT(m_area), "workspace-rename-handler",
        new std::function<void(const std::string&)>(
            [this](const std::string& stackId) { startInlineStackRename(stackId); }),
        +[](gpointer p) { delete static_cast<std::function<void(const std::string&)>*>(p); });

    gtk_widget_show(m_area);

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

    // Setup GTK Drag and Drop Destination
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
    cancelInlineStackRename();

    if (m_state.animation.zoomSettlingTimerId != 0) {
        g_source_remove(m_state.animation.zoomSettlingTimerId);
        m_state.animation.zoomSettlingTimerId = 0;
    }
    if (m_state.animation.glideTimerId != 0) {
        g_source_remove(m_state.animation.glideTimerId);
        m_state.animation.glideTimerId = 0;
    }
    if (m_state.animation.flashTimerId != 0) {
        g_source_remove(m_state.animation.flashTimerId);
        m_state.animation.flashTimerId = 0;
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
        self->m_state.animation.zoomSettlingTimerId = 0;
        self->onZoomSettled();
    }
    return G_SOURCE_REMOVE;
}

void WorkspaceView::onZoomSettled() {
    if (!m_excerptTileCache || !m_area || !GTK_IS_WIDGET(m_area)) {
        return;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const FluidCore::Rectangle viewport{m_state.viewport.originX, m_state.viewport.originY,
                                        alloc.width / m_state.viewport.zoom,
                                        alloc.height / m_state.viewport.zoom};

    auto visibleNodes = m_api.queryVisibleNodes(viewport);
    for (const auto* node : visibleNodes) {
        const auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node);
        if (excerpt && excerpt->isImageExcerpt()) {
            m_excerptTileCache->requestCropAsync(
                excerpt->id(), excerpt->sourceDocId(), excerpt->sourcePageNo(),
                excerpt->sourceNormalizedRect(), excerpt->bounds().w - 16.0,
                excerpt->bounds().h - 40.0, m_state.viewport.zoom);
        }
    }
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::zoomAt(double factor, double focalScreenX, double focalScreenY) {
    const double oldZoom = m_state.viewport.zoom;
    const double newZoom = std::clamp(oldZoom * factor, kMinZoom, kMaxZoom);
    if (std::abs(newZoom - oldZoom) < 1e-6)
        return;

    const double focalWorldX = m_state.viewport.originX + focalScreenX / oldZoom;
    const double focalWorldY = m_state.viewport.originY + focalScreenY / oldZoom;

    m_state.viewport.zoom = newZoom;
    m_state.viewport.originX = focalWorldX - focalScreenX / newZoom;
    m_state.viewport.originY = focalWorldY - focalScreenY / newZoom;

    if (m_state.animation.zoomSettlingTimerId != 0) {
        g_source_remove(m_state.animation.zoomSettlingTimerId);
        m_state.animation.zoomSettlingTimerId = 0;
    }
    m_state.animation.zoomSettlingTimerId = g_timeout_add(150, zoomSettlingTimeoutCallback, this);

    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::setZoom(double zoom) {
    if (!m_area || !GTK_IS_WIDGET(m_area)) return;
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double cx = alloc.width > 0 ? alloc.width / 2.0 : 0.0;
    const double cy = alloc.height > 0 ? alloc.height / 2.0 : 0.0;
    zoomAt(zoom / m_state.viewport.zoom, cx, cy);
}

void WorkspaceView::panBy(double dxScreen, double dyScreen) {
    m_state.viewport.originX -= dxScreen / m_state.viewport.zoom;
    m_state.viewport.originY -= dyScreen / m_state.viewport.zoom;
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::centerOn(double worldX, double worldY) {
    if (!m_area || !GTK_IS_WIDGET(m_area)) return;
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double vw = alloc.width > 0 ? alloc.width / m_state.viewport.zoom : 800.0;
    const double vh = alloc.height > 0 ? alloc.height / m_state.viewport.zoom : 600.0;

    m_state.viewport.originX = worldX - vw / 2.0;
    m_state.viewport.originY = worldY - vh / 2.0;
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::glideToWorldCoord(double targetWorldX, double targetWorldY) {
    if (m_state.animation.glideTimerId != 0) {
        g_source_remove(m_state.animation.glideTimerId);
        m_state.animation.glideTimerId = 0;
    }

    if (!m_area || !GTK_IS_WIDGET(m_area)) return;
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double vw = alloc.width > 0 ? alloc.width / m_state.viewport.zoom : 800.0;
    const double vh = alloc.height > 0 ? alloc.height / m_state.viewport.zoom : 600.0;

    m_state.animation.glideStartX = m_state.viewport.originX;
    m_state.animation.glideStartY = m_state.viewport.originY;
    m_state.animation.glideTargetX = targetWorldX - vw / 2.0;
    m_state.animation.glideTargetY = targetWorldY - vh / 2.0;
    m_state.animation.glideStartTimeUs = g_get_real_time();

    m_state.animation.glideTimerId = g_timeout_add(
        16,
        +[](gpointer data) -> gboolean {
            auto* self = static_cast<WorkspaceView*>(data);
            if (!self || !self->m_area || !GTK_IS_WIDGET(self->m_area)) {
                return G_SOURCE_REMOVE;
            }

            const gint64 elapsedUs = g_get_real_time() - self->m_state.animation.glideStartTimeUs;
            const double elapsedSec = static_cast<double>(elapsedUs) / 1000000.0;
            const double totalDurationSec = 0.25;

            if (elapsedSec >= totalDurationSec) {
                self->m_state.viewport.originX = self->m_state.animation.glideTargetX;
                self->m_state.viewport.originY = self->m_state.animation.glideTargetY;
                self->m_state.animation.glideTimerId = 0;
                if (self->m_area && GTK_IS_WIDGET(self->m_area)) {
                    gtk_widget_queue_draw(self->m_area);
                }
                return G_SOURCE_REMOVE;
            }

            // Cubic ease-out: 1 - (1 - t)^3
            const double t = elapsedSec / totalDurationSec;
            const double easeOut = 1.0 - std::pow(1.0 - t, 3.0);

            self->m_state.viewport.originX =
                self->m_state.animation.glideStartX +
                (self->m_state.animation.glideTargetX - self->m_state.animation.glideStartX) *
                    easeOut;
            self->m_state.viewport.originY =
                self->m_state.animation.glideStartY +
                (self->m_state.animation.glideTargetY - self->m_state.animation.glideStartY) *
                    easeOut;

            if (self->m_area && GTK_IS_WIDGET(self->m_area)) {
                gtk_widget_queue_draw(self->m_area);
            }
            return G_SOURCE_CONTINUE;
        },
        this);
}

void WorkspaceView::flashExcerptCard(const std::string& cardId) {
    if (m_state.animation.flashTimerId != 0) {
        g_source_remove(m_state.animation.flashTimerId);
        m_state.animation.flashTimerId = 0;
    }

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

    m_state.animation.flashCardId = cardId;
    m_state.animation.flashAlpha = 1.0;
    m_state.animation.flashStartTimeUs = g_get_real_time();

    m_state.animation.flashTimerId = g_timeout_add(
        16,
        +[](gpointer data) -> gboolean {
            auto* self = static_cast<WorkspaceView*>(data);
            if (!self || !self->m_area || !GTK_IS_WIDGET(self->m_area)) {
                return G_SOURCE_REMOVE;
            }

            const gint64 elapsedUs = g_get_real_time() - self->m_state.animation.flashStartTimeUs;
            const double elapsedSec = static_cast<double>(elapsedUs) / 1000000.0;
            const double totalDurationSec = 1.2;

            if (elapsedSec >= totalDurationSec) {
                self->m_state.animation.flashCardId.clear();
                self->m_state.animation.flashAlpha = 0.0;
                self->m_state.animation.flashTimerId = 0;
                if (self->m_area && GTK_IS_WIDGET(self->m_area)) {
                    gtk_widget_queue_draw(self->m_area);
                }
                return G_SOURCE_REMOVE;
            }

            const double progress = elapsedSec / totalDurationSec;
            self->m_state.animation.flashAlpha = (1.0 - progress) * (1.0 - progress);

            if (self->m_area && GTK_IS_WIDGET(self->m_area)) {
                gtk_widget_queue_draw(self->m_area);
            }
            return G_SOURCE_CONTINUE;
        },
        this);

    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::setSearchResults(std::vector<FluidCore::WorkspaceMatch> matches,
                                     const std::string& query, int activeIndex) {
    m_state.search.matches = std::move(matches);
    m_state.search.query = query;
    m_state.search.active = !m_state.search.matches.empty() || !query.empty();
    m_state.search.matchingNodeIds.clear();
    m_state.search.matchingTopLevelNodeIds.clear();

    for (const auto& match : m_state.search.matches) {
        m_state.search.matchingNodeIds.push_back(match.nodeId);
        m_state.search.matchingTopLevelNodeIds.push_back(match.topLevelNodeId);
    }

    if (m_state.search.matches.empty()) {
        m_state.search.activeMatchIndex = -1;
    } else {
        m_state.search.activeMatchIndex =
            std::clamp(activeIndex, 0, static_cast<int>(m_state.search.matches.size() - 1));
        const auto& match = m_state.search.matches[m_state.search.activeMatchIndex];
        const double cx = match.bounds.x + match.bounds.w / 2.0;
        const double cy = match.bounds.y + match.bounds.h / 2.0;
        glideToWorldCoord(cx, cy);
        flashExcerptCard(match.nodeId);
    }

    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::clearSearch() {
    m_state.search.active = false;
    m_state.search.query.clear();
    m_state.search.matches.clear();
    m_state.search.activeMatchIndex = -1;
    m_state.search.matchingNodeIds.clear();
    m_state.search.matchingTopLevelNodeIds.clear();
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::navigateSearch(int direction) {
    if (m_state.search.matches.empty()) {
        return;
    }

    const int total = static_cast<int>(m_state.search.matches.size());
    int nextIdx = m_state.search.activeMatchIndex + direction;
    if (nextIdx >= total) {
        nextIdx = 0;
    } else if (nextIdx < 0) {
        nextIdx = total - 1;
    }

    m_state.search.activeMatchIndex = nextIdx;
    const auto& match = m_state.search.matches[nextIdx];
    const double cx = match.bounds.x + match.bounds.w / 2.0;
    const double cy = match.bounds.y + match.bounds.h / 2.0;
    glideToWorldCoord(cx, cy);
    flashExcerptCard(match.nodeId);

    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::setSpacePressed(bool pressed) {
    if (m_state.isSpacePressed == pressed)
        return;
    m_state.isSpacePressed = pressed;
    if (m_area && GTK_IS_WIDGET(m_area)) {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            if (m_state.isSpacePressed && !m_state.isPanning) {
                GdkDisplay* display = gdk_window_get_display(win);
                GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND1);
                gdk_window_set_cursor(win, cursor);
                if (cursor)
                    g_object_unref(cursor);
            } else if (!m_state.isSpacePressed && !m_state.isPanning) {
                gdk_window_set_cursor(win, nullptr);
            }
        }
    }
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
        m_state.viewport.zoom = std::clamp(std::min(fitZoomX, fitZoomY), 0.2, 1.5);
        const double centerWorldX = bounds.x + bounds.w / 2.0;
        const double centerWorldY = bounds.y + bounds.h / 2.0;
        m_state.viewport.originX = centerWorldX - (viewW / m_state.viewport.zoom) / 2.0;
        m_state.viewport.originY = centerWorldY - (viewH / m_state.viewport.zoom) / 2.0;
    } else {
        m_state.viewport.zoom = 1.0;
        m_state.viewport.originX = 0.0;
        m_state.viewport.originY = 0.0;
    }
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::setMinimapVisible(bool visible) {
    if (m_state.showMinimap != visible) {
        m_state.showMinimap = visible;
        if (m_area && GTK_IS_WIDGET(m_area)) {
            gtk_widget_queue_draw(m_area);
        }
    }
}

void WorkspaceView::startInlineStackRename(const std::string& stackId) {
    if (m_activeRenamePopover) {
        commitInlineStackRename();
    }

    const FluidCore::CardStackNode* stack = nullptr;
    const FluidCore::Rectangle wsBounds = m_api.getWorkspaceBounds();
    for (const auto* node : m_api.queryVisibleNodes(wsBounds)) {
        if (const auto* s = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            if (s->id() == stackId) {
                stack = s;
                break;
            }
        }
    }
    if (!stack || !m_area) {
        return;
    }

    const double zoom = m_state.viewport.zoom;
    const auto hdrRect = FluidCore::CardLayoutEngine::getStackHeaderRect(
        stack->bounds(), m_state.viewport.originX, m_state.viewport.originY, zoom);

    const double chevronSize = std::min(18.0 * zoom, hdrRect.h * 0.7);
    const double titleStartX = hdrRect.x + 8.0 * zoom + chevronSize + 6.0 * zoom;
    const double badgeSpace = 70.0 * zoom;
    const double entryW = std::clamp(hdrRect.w - (titleStartX - hdrRect.x) - badgeSpace, 140.0, 360.0);

    GtkWidget* popover = gtk_popover_new(m_area);
    m_activeRenamePopover = popover;
    m_activeRenameStackId = stackId;

    GdkRectangle pointRect = {static_cast<int>(titleStartX), static_cast<int>(hdrRect.y),
                              static_cast<int>(entryW), static_cast<int>(hdrRect.h)};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &pointRect);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_popover_set_modal(GTK_POPOVER(popover), TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);

    GtkWidget* entry = gtk_entry_new();
    m_activeRenameEntry = entry;
    gtk_entry_set_text(GTK_ENTRY(entry), stack->title().c_str());
    gtk_widget_set_size_request(entry, static_cast<int>(entryW), -1);

    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = "entry { "
                      "  background: #0f172a; "
                      "  color: #f8fafc; "
                      "  border: 1.5px solid #38bdf8; "
                      "  border-radius: 4px; "
                      "  padding: 3px 8px; "
                      "  font-weight: bold; "
                      "  font-size: 12px; "
                      "}";
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    GtkStyleContext* ctx = gtk_widget_get_style_context(entry);
    gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(popover), box);

    g_signal_connect(entry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer data) {
                         auto* ws = static_cast<WorkspaceView*>(data);
                         if (ws) ws->commitInlineStackRename();
                     }), this);

    g_signal_connect(entry, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         auto* ws = static_cast<WorkspaceView*>(data);
                         if (ws && event->keyval == GDK_KEY_Escape) {
                             ws->cancelInlineStackRename();
                             return TRUE;
                         }
                         return FALSE;
                     }), this);

    g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover*, gpointer data) {
                         auto* ws = static_cast<WorkspaceView*>(data);
                         if (ws) ws->commitInlineStackRename();
                     }), this);

    gtk_widget_show_all(popover);
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_widget_grab_focus(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

void WorkspaceView::commitInlineStackRename() {
    if (!m_activeRenamePopover) return;

    GtkWidget* popover = m_activeRenamePopover;
    GtkWidget* entry = m_activeRenameEntry;
    m_activeRenamePopover = nullptr;
    m_activeRenameEntry = nullptr;

    const std::string stackId = m_activeRenameStackId;
    m_activeRenameStackId.clear();

    if (entry && GTK_IS_ENTRY(entry)) {
        const gchar* newText = gtk_entry_get_text(GTK_ENTRY(entry));
        if (newText && newText[0] != '\0') {
            m_api.setStackTitle(stackId, std::string(newText));
        }
    }

    if (popover && GTK_IS_WIDGET(popover)) {
        gtk_widget_destroy(popover);
    }
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_grab_focus(m_area);
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::cancelInlineStackRename() {
    if (!m_activeRenamePopover) return;

    GtkWidget* popover = m_activeRenamePopover;
    m_activeRenamePopover = nullptr;
    m_activeRenameEntry = nullptr;
    m_activeRenameStackId.clear();

    if (popover && GTK_IS_WIDGET(popover)) {
        gtk_widget_destroy(popover);
    }
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_grab_focus(m_area);
        gtk_widget_queue_draw(m_area);
    }
}

void WorkspaceView::setTool(const std::string& tool) {
    if (m_state.inking.currentTool != tool) {
        m_state.inking.currentTool = tool;
        if (tool == "highlighter") {
            m_state.inking.currentColor = 0xFFFF00;
            m_state.inking.currentWidth = 14.0;
        } else if (tool == "pen") {
            m_state.inking.currentColor = 0x000000;
            m_state.inking.currentWidth = 2.0;
        } else if (tool == "eraser") {
            m_state.inking.currentWidth = 24.0;
        }
        m_state.inking.isDrawing = false;
        m_state.inking.hasWetSegment = false;

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
    m_state.isDropHovering = true;
    m_state.dropHoverScreenX = x;
    m_state.dropHoverScreenY = y;
    gdk_drag_status(context, GDK_ACTION_COPY, time);
    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_queue_draw(m_area);
    }
    return TRUE;
}

void WorkspaceView::onDragLeave(GdkDragContext*, guint) {
    if (m_state.isDropHovering) {
        m_state.isDropHovering = false;
        if (m_area && GTK_IS_WIDGET(m_area)) {
            gtk_widget_queue_draw(m_area);
        }
    }
}

void WorkspaceView::onDragDataReceived(GdkDragContext* context, gint x, gint y,
                                       GtkSelectionData* data, guint info, guint time) {
    WorkspaceInteraction::handleExcerptDrop(m_state, m_api, m_area, context, x, y, data, info, time,
                                            m_onExcerptAdded);
}

void WorkspaceView::draw(cairo_t* cr, int width, int height) {
    WorkspaceRenderer::draw(cr, m_state, m_api, m_excerptTileCache, width, height);
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

    if (WorkspaceInteraction::minimapHitTest(m_state, event->x, event->y, alloc.width,
                                             alloc.height)) {
        if (event->button == GDK_BUTTON_PRIMARY) {
            m_state.isMinimapDragging = true;
            WorkspaceInteraction::handleMinimapInteraction(m_state, m_api, m_area, event->x,
                                                           event->y, alloc.width, alloc.height);
            return TRUE;
        }
    }

    if (event->button == GDK_BUTTON_MIDDLE ||
        (event->button == GDK_BUTTON_PRIMARY &&
         (m_state.isSpacePressed || (event->state & GDK_MOD1_MASK) ||
          m_state.inking.currentTool == "pan"))) {
        m_state.isPanning = true;
        m_state.lastMouseX = event->x;
        m_state.lastMouseY = event->y;

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
        FluidCore::Point wPt = screenToWorld(event->x, event->y);
        std::string hitEdge = WorkspaceInteraction::hitTestEdgeAtWorldPoint(m_api, wPt, 10.0);
        if (!hitEdge.empty()) {
            WorkspaceInteraction::showEdgeContextMenu(m_state, m_api, m_area, hitEdge, event);
            return TRUE;
        }

        std::string parentStackId;
        const auto* hitChild =
            WorkspaceInteraction::hitTestChildNodeAtWorldPoint(m_api, wPt, &parentStackId);
        if (hitChild && !parentStackId.empty()) {
            WorkspaceInteraction::showNodeContextMenu(m_state, m_api, m_area, hitChild,
                                                      parentStackId, event);
            return TRUE;
        }

        const auto* hitNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(m_api, wPt);
        if (hitNode) {
            WorkspaceInteraction::showNodeContextMenu(m_state, m_api, m_area, hitNode, "", event);
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

        // 1. Check if click hits Stack Chevron button [▼]/[▶]
        for (const auto* node : visibleNodes) {
            if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                const auto chevRect = FluidCore::CardLayoutEngine::getStackChevronRect(
                    stack->bounds(), m_state.viewport.originX, m_state.viewport.originY,
                    m_state.viewport.zoom);
                if (event->x >= chevRect.x && event->x <= chevRect.x + chevRect.w &&
                    event->y >= chevRect.y && event->y <= chevRect.y + chevRect.h) {
                    m_api.toggleStackCollapsed(stack->id());
                    if (m_area && GTK_IS_WIDGET(m_area)) {
                        gtk_widget_queue_draw(m_area);
                    }
                    return TRUE;
                }
            }
        }

        // 2. Check if click hits any visible ExcerptCardNode's [ ↗ Anchor ] pill button
        if (m_state.viewport.zoom >= 0.2) {
            for (const auto* node : visibleNodes) {
                if (const auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
                    const auto pillRect = FluidCore::CardLayoutEngine::getExcerptAnchorPillRect(
                        node->bounds(), m_state.viewport.originX, m_state.viewport.originY,
                        m_state.viewport.zoom);
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
                                const auto pillRect =
                                    FluidCore::CardLayoutEngine::getExcerptAnchorPillRect(
                                        cExcerpt->bounds(), m_state.viewport.originX,
                                        m_state.viewport.originY, m_state.viewport.zoom);
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

        // 3. Double-Click: toggle collapse if on chevron; inline rename on title area; otherwise center on point
        if (event->type == GDK_2BUTTON_PRESS) {
            for (const auto* node : visibleNodes) {
                if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                    const auto hdrRect = FluidCore::CardLayoutEngine::getStackHeaderRect(
                        stack->bounds(), m_state.viewport.originX, m_state.viewport.originY,
                        m_state.viewport.zoom);
                    if (event->x >= hdrRect.x && event->x <= hdrRect.x + hdrRect.w &&
                        event->y >= hdrRect.y && event->y <= hdrRect.y + hdrRect.h) {
                        const auto chevRect = FluidCore::CardLayoutEngine::getStackChevronRect(
                            stack->bounds(), m_state.viewport.originX, m_state.viewport.originY,
                            m_state.viewport.zoom);
                        if (event->x >= chevRect.x && event->x <= chevRect.x + chevRect.w &&
                            event->y >= chevRect.y && event->y <= chevRect.y + chevRect.h) {
                            m_api.toggleStackCollapsed(stack->id());
                            if (m_area && GTK_IS_WIDGET(m_area)) {
                                gtk_widget_queue_draw(m_area);
                            }
                        } else {
                            startInlineStackRename(stack->id());
                        }
                        return TRUE;
                    }
                }
            }

            FluidCore::Point worldPt = screenToWorld(event->x, event->y);
            centerOn(worldPt.x, worldPt.y);
            return TRUE;
        }

        const bool isDrawingOrConnecting =
            (m_state.inking.currentTool == "pen" || m_state.inking.currentTool == "highlighter" ||
             m_state.inking.currentTool == "eraser" || m_state.inking.currentTool == "connector");

        if (!isDrawingOrConnecting) {
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            if (m_state.inking.currentTool == "select") {
                std::string hitEdge =
                    WorkspaceInteraction::hitTestEdgeAtWorldPoint(m_api, wPt, 8.0);
                if (!hitEdge.empty()) {
                    m_state.selectedEdgeId = hitEdge;
                    m_state.selectedNodeId.reset();
                    if (m_area && GTK_IS_WIDGET(m_area)) {
                        gtk_widget_queue_draw(m_area);
                    }
                    return TRUE;
                } else {
                    if (m_state.selectedEdgeId.has_value()) {
                        m_state.selectedEdgeId.reset();
                        if (m_area && GTK_IS_WIDGET(m_area)) {
                            gtk_widget_queue_draw(m_area);
                        }
                    }
                }
            }

            std::string parentStackId;
            const auto* hitChild =
                WorkspaceInteraction::hitTestChildNodeAtWorldPoint(m_api, wPt, &parentStackId);
            if (hitChild && !parentStackId.empty()) {
                m_state.dragSnap.dragPending = true;
                m_state.dragSnap.dragStartScreenX = event->x;
                m_state.dragSnap.dragStartScreenY = event->y;
                m_state.dragSnap.dragCandidateNodeId = hitChild->id();
                m_state.dragSnap.dragCandidateIsChild = true;
                m_state.dragSnap.dragCandidateParentStackId = parentStackId;
                const auto b = hitChild->bounds();
                m_state.dragSnap.dragInitialWorldPos = {b.x, b.y};
                m_state.dragSnap.dragOffsetWorld = {wPt.x - b.x, wPt.y - b.y};
                m_state.selectedNodeId = hitChild->id();
                if (m_area && GTK_IS_WIDGET(m_area)) {
                    gtk_widget_queue_draw(m_area);
                }
                return TRUE;
            }

            const auto* hitNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(m_api, wPt);
            if (hitNode) {
                m_state.dragSnap.dragPending = true;
                m_state.dragSnap.dragStartScreenX = event->x;
                m_state.dragSnap.dragStartScreenY = event->y;
                m_state.dragSnap.dragCandidateNodeId = hitNode->id();
                m_state.dragSnap.dragCandidateIsChild = false;
                m_state.dragSnap.dragCandidateParentStackId.clear();
                m_state.dragSnap.dragInitialWorldPos = m_api.getNodePosition(hitNode->id());
                m_state.dragSnap.dragOffsetWorld = {wPt.x - m_state.dragSnap.dragInitialWorldPos.x,
                                                    wPt.y - m_state.dragSnap.dragInitialWorldPos.y};
                m_state.selectedNodeId = hitNode->id();
                if (m_area && GTK_IS_WIDGET(m_area)) {
                    gtk_widget_queue_draw(m_area);
                }
                return TRUE;
            } else {
                if (m_state.selectedNodeId.has_value()) {
                    m_state.selectedNodeId.reset();
                    if (m_area && GTK_IS_WIDGET(m_area)) {
                        gtk_widget_queue_draw(m_area);
                    }
                }
            }
        }

        if (m_state.inking.currentTool == "connector") {
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            const auto* hitNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(m_api, wPt);
            if (hitNode) {
                m_state.connector.isConnecting = true;
                m_state.connector.connectorSourceNodeId = hitNode->id();
                m_state.connector.connectorStartWorld = wPt;
                m_state.connector.connectorCurrentWorld = wPt;
                m_state.connector.connectorTargetHoverNodeId.clear();
                if (m_area && GTK_IS_WIDGET(m_area)) {
                    gtk_widget_queue_draw(m_area);
                }
                return TRUE;
            }
        }

        if (m_state.inking.currentTool == "pen" || m_state.inking.currentTool == "highlighter") {
            m_state.inking.isDrawing = true;
            m_state.inking.activeStroke = FluidCore::Stroke{};

            static std::size_t s_strokeCounter = 1;
            m_state.inking.activeStroke.id = "stroke-" + std::to_string(s_strokeCounter++);
            m_state.inking.activeStroke.tool = m_state.inking.currentTool;
            m_state.inking.activeStroke.color = m_state.inking.currentColor;
            m_state.inking.activeStroke.width = m_state.inking.currentWidth;
            m_state.inking.activeStroke.timestamp = g_get_real_time();
            m_state.inking.activeSegments.clear();
            m_state.inking.hasWetSegment = false;

            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            m_state.inking.stabilizer.beginStroke(
                FluidCoreApp::StrokeStabilizer::Point2D{wPt.x, wPt.y}, 1.0, g_get_real_time());
            if (m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
            return TRUE;
        } else if (m_state.inking.currentTool == "eraser") {
            m_state.inking.isDrawing = true;
            m_state.lastMouseX = event->x;
            m_state.lastMouseY = event->y;
            FluidCore::Point wPt = screenToWorld(event->x, event->y);
            const double wRadius = 30.0 / m_state.viewport.zoom;
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

            std::string hitEdge =
                WorkspaceInteraction::hitTestEdgeAtWorldPoint(m_api, wPt, wRadius);
            if (!hitEdge.empty()) {
                m_api.removeEdge(hitEdge);
                if (m_state.selectedEdgeId && *m_state.selectedEdgeId == hitEdge) {
                    m_state.selectedEdgeId.reset();
                }
                removed = true;
            }

            if (removed && m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
            return TRUE;
        }
    }

    return FALSE;
}

gboolean WorkspaceView::onButtonRelease(GdkEventButton* event) {
    (void)event;
    if (m_state.isMinimapDragging) {
        m_state.isMinimapDragging = false;
        return TRUE;
    }

    if (m_state.isPanning) {
        m_state.isPanning = false;
        if (m_area && GTK_IS_WIDGET(m_area)) {
            GdkWindow* win = gtk_widget_get_window(m_area);
            if (win) {
                if (m_state.isSpacePressed) {
                    GdkDisplay* display = gdk_window_get_display(win);
                    GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND1);
                    gdk_window_set_cursor(win, cursor);
                    if (cursor)
                        g_object_unref(cursor);
                } else {
                    gdk_window_set_cursor(win, nullptr);
                }
            }
        }
        return TRUE;
    }

    if (m_state.dragSnap.isDraggingCard) {
        if (m_state.dragSnap.activeSnapType == FluidCore::SnapType::StackMerge &&
            !m_state.dragSnap.activeMergeTargetId.empty()) {
            m_api.mergeNodesIntoStack(m_state.dragSnap.dragCandidateNodeId,
                                      m_state.dragSnap.activeMergeTargetId);
        }

        m_state.dragSnap.isDraggingCard = false;
        m_state.dragSnap.dragPending = false;
        m_state.dragSnap.dragCandidateNodeId.clear();
        m_state.dragSnap.activeMergeTargetId.clear();
        m_state.dragSnap.activeSnapGuideLines.clear();
        m_state.dragSnap.activeSnapType = FluidCore::SnapType::None;

        if (m_area && GTK_IS_WIDGET(m_area)) {
            GdkWindow* win = gtk_widget_get_window(m_area);
            if (win) {
                gdk_window_set_cursor(win, nullptr);
            }
            gtk_widget_queue_draw(m_area);
        }
        return TRUE;
    }

    m_state.dragSnap.dragPending = false;

    if (m_state.connector.isConnecting) {
        m_state.connector.isConnecting = false;
        FluidCore::Point wPt = screenToWorld(event->x, event->y);
        const auto* targetNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(m_api, wPt);
        if (targetNode && targetNode->id() != m_state.connector.connectorSourceNodeId) {
            FluidCore::Color edgeColor{
                static_cast<unsigned char>((m_state.inking.currentColor >> 16) & 0xFF),
                static_cast<unsigned char>((m_state.inking.currentColor >> 8) & 0xFF),
                static_cast<unsigned char>(m_state.inking.currentColor & 0xFF), 255};
            if (m_state.inking.currentColor == 0x000000) {
                edgeColor = {30, 144, 255, 255};
            }
            m_api.createInkLink(m_state.connector.connectorSourceNodeId, targetNode->id(),
                                edgeColor);
        }
        m_state.connector.connectorSourceNodeId.clear();
        m_state.connector.connectorTargetHoverNodeId.clear();
        if (m_area && GTK_IS_WIDGET(m_area)) {
            gtk_widget_queue_draw(m_area);
        }
        return TRUE;
    }

    if (m_state.inking.isDrawing) {
        m_state.inking.isDrawing = false;
        if (m_state.inking.currentTool == "pen" || m_state.inking.currentTool == "highlighter") {
            m_state.inking.stabilizer.endStroke();
            m_state.inking.hasWetSegment = false;

            m_state.inking.activeStroke.points.clear();
            for (const auto& sample : m_state.inking.stabilizer.rawSamples()) {
                m_state.inking.activeStroke.points.push_back(
                    FluidCore::XoppPoint{sample.point.x, sample.point.y});
            }

            if (!m_state.inking.activeStroke.points.empty()) {
                bool convertedToConnector = false;

                if (m_state.inking.currentTool == "pen" &&
                    m_state.inking.activeStroke.points.size() >= 2) {
                    const auto& ptStart = m_state.inking.activeStroke.points.front();
                    const auto& ptEnd = m_state.inking.activeStroke.points.back();
                    const auto* srcNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(
                        m_api, FluidCore::Point{ptStart.x, ptStart.y});
                    const auto* dstNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(
                        m_api, FluidCore::Point{ptEnd.x, ptEnd.y});

                    if (srcNode && dstNode && srcNode->id() != dstNode->id()) {
                        const double dx = ptEnd.x - ptStart.x;
                        const double dy = ptEnd.y - ptStart.y;
                        const double chordDist = std::sqrt(dx * dx + dy * dy);

                        double totalArcLen = 0.0;
                        for (size_t i = 1; i < m_state.inking.activeStroke.points.size(); ++i) {
                            const double segDx = m_state.inking.activeStroke.points[i].x -
                                                 m_state.inking.activeStroke.points[i - 1].x;
                            const double segDy = m_state.inking.activeStroke.points[i].y -
                                                 m_state.inking.activeStroke.points[i - 1].y;
                            totalArcLen += std::sqrt(segDx * segDx + segDy * segDy);
                        }

                        const double straightness =
                            totalArcLen > 1e-6 ? (chordDist / totalArcLen) : 1.0;

                        if (straightness >= 0.82) {
                            FluidCore::Color edgeColor{
                                static_cast<unsigned char>((m_state.inking.currentColor >> 16) &
                                                           0xFF),
                                static_cast<unsigned char>((m_state.inking.currentColor >> 8) &
                                                           0xFF),
                                static_cast<unsigned char>(m_state.inking.currentColor & 0xFF),
                                255};
                            if (m_state.inking.currentColor == 0x000000) {
                                edgeColor = {30, 144, 255, 255};
                            }
                            m_api.createInkLink(srcNode->id(), dstNode->id(), edgeColor);
                            convertedToConnector = true;
                        }
                    }
                }

                if (!convertedToConnector) {
                    m_api.insertNode(
                        std::make_unique<FluidCore::CanvasStrokeNode>(m_state.inking.activeStroke));
                }
            }
            m_state.inking.activeSegments.clear();
            if (m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
        }
        return TRUE;
    }

    return FALSE;
}

gboolean WorkspaceView::onMotion(GdkEventMotion* event) {
    if (!m_area || !GTK_IS_WIDGET(m_area)) return FALSE;
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);

    if (m_state.isMinimapDragging) {
        WorkspaceInteraction::handleMinimapInteraction(m_state, m_api, m_area, event->x, event->y,
                                                       alloc.width, alloc.height);
        return TRUE;
    }

    if (m_state.isPanning) {
        const double dx = event->x - m_state.lastMouseX;
        const double dy = event->y - m_state.lastMouseY;
        m_state.lastMouseX = event->x;
        m_state.lastMouseY = event->y;
        panBy(dx, dy);
        return TRUE;
    }

    if (m_state.dragSnap.dragPending && !m_state.dragSnap.isDraggingCard) {
        const double dragDist = std::hypot(event->x - m_state.dragSnap.dragStartScreenX,
                                           event->y - m_state.dragSnap.dragStartScreenY);
        if (dragDist >= 6.0) {
            m_state.dragSnap.isDraggingCard = true;
            if (m_state.dragSnap.dragCandidateIsChild &&
                !m_state.dragSnap.dragCandidateParentStackId.empty()) {
                m_api.extractChildFromStack(m_state.dragSnap.dragCandidateParentStackId,
                                            m_state.dragSnap.dragCandidateNodeId,
                                            m_state.dragSnap.dragInitialWorldPos);
                m_state.dragSnap.dragCandidateIsChild = false;
                m_state.dragSnap.dragCandidateParentStackId.clear();
            }
        }
    }

    if (m_state.dragSnap.isDraggingCard && !m_state.dragSnap.dragCandidateNodeId.empty()) {
        const FluidCore::Point currentWorld = screenToWorld(event->x, event->y);
        const FluidCore::Rectangle nodeBounds =
            m_api.getNodeBounds(m_state.dragSnap.dragCandidateNodeId);
        const FluidCore::Rectangle proposedBounds{
            currentWorld.x - m_state.dragSnap.dragOffsetWorld.x,
            currentWorld.y - m_state.dragSnap.dragOffsetWorld.y, nodeBounds.w, nodeBounds.h};

        const double snapThresholdWorld = 16.0 / m_state.viewport.zoom;
        const FluidCore::SnapResult snapRes = m_api.solveSnap(proposedBounds, snapThresholdWorld,
                                                              m_state.dragSnap.dragCandidateNodeId);

        if (snapRes.type == FluidCore::SnapType::StackMerge) {
            m_state.dragSnap.activeSnapType = FluidCore::SnapType::StackMerge;
            m_state.dragSnap.activeMergeTargetId = snapRes.targetNodeId;
            m_state.dragSnap.activeSnapGuideLines.clear();
            m_state.dragSnap.draggedGhostBounds = proposedBounds;
            m_api.updateNodePosition(m_state.dragSnap.dragCandidateNodeId, proposedBounds.x,
                                     proposedBounds.y);
        } else if (snapRes.type == FluidCore::SnapType::MagneticSnap) {
            m_state.dragSnap.activeSnapType = FluidCore::SnapType::MagneticSnap;
            m_state.dragSnap.activeMergeTargetId.clear();
            m_state.dragSnap.activeSnapGuideLines = snapRes.guideLines;
            m_state.dragSnap.draggedGhostBounds = snapRes.snappedBounds;
            m_api.updateNodePosition(m_state.dragSnap.dragCandidateNodeId, snapRes.snappedBounds.x,
                                     snapRes.snappedBounds.y);
        } else {
            m_state.dragSnap.activeSnapType = FluidCore::SnapType::None;
            m_state.dragSnap.activeMergeTargetId.clear();
            m_state.dragSnap.activeSnapGuideLines.clear();
            m_state.dragSnap.draggedGhostBounds = proposedBounds;
            m_api.updateNodePosition(m_state.dragSnap.dragCandidateNodeId, proposedBounds.x,
                                     proposedBounds.y);
        }

        if (m_area && GTK_IS_WIDGET(m_area)) {
            GdkWindow* win = gtk_widget_get_window(m_area);
            if (win) {
                GdkDisplay* display = gdk_window_get_display(win);
                GdkCursor* grabCursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
                gdk_window_set_cursor(win, grabCursor);
                if (grabCursor)
                    g_object_unref(grabCursor);
            }
            gtk_widget_queue_draw(m_area);
        }
        return TRUE;
    }

    if (m_state.connector.isConnecting) {
        m_state.connector.connectorCurrentWorld = screenToWorld(event->x, event->y);
        const auto* targetNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(
            m_api, m_state.connector.connectorCurrentWorld);
        m_state.connector.connectorTargetHoverNodeId =
            (targetNode && targetNode->id() != m_state.connector.connectorSourceNodeId)
                ? targetNode->id()
                : "";
        if (m_area && GTK_IS_WIDGET(m_area)) {
            gtk_widget_queue_draw(m_area);
        }
        return TRUE;
    }

    if (m_state.inking.isDrawing) {
        FluidCore::Point wPt = screenToWorld(event->x, event->y);

        if (m_state.inking.currentTool == "pen" || m_state.inking.currentTool == "highlighter") {
            auto result = m_state.inking.stabilizer.pushPoint(
                FluidCoreApp::StrokeStabilizer::Point2D{wPt.x, wPt.y}, 1.0, g_get_real_time());
            for (const auto& seg : result.newlyCommitted) {
                m_state.inking.activeSegments.push_back(seg);
            }
            m_state.inking.hasWetSegment = result.hasWetSegment;
            m_state.inking.activeWetTip = result.wetTip;
            if (m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
        } else if (m_state.inking.currentTool == "eraser") {
            const double wRadius = 30.0 / m_state.viewport.zoom;
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
            if (removed && m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
        }
        return TRUE;
    }

    if (m_area && GTK_IS_WIDGET(m_area)) {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            GdkDisplay* display = gdk_window_get_display(win);

            std::string newHoveredId;
            bool isHoveringChevron = false;
            if (m_state.viewport.zoom >= 0.2) {
                const FluidCore::Point worldTopLeft = screenToWorld(0, 0);
                const FluidCore::Point worldBottomRight = screenToWorld(
                    alloc.width > 0 ? alloc.width : 800, alloc.height > 0 ? alloc.height : 600);
                const FluidCore::Rectangle viewWorldRect{
                    worldTopLeft.x, worldTopLeft.y, std::max(0.0, worldBottomRight.x - worldTopLeft.x),
                    std::max(0.0, worldBottomRight.y - worldTopLeft.y)};

                auto visibleNodes = m_api.queryVisibleNodes(viewWorldRect);
                for (const auto* node : visibleNodes) {
                    if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
                        const auto chevRect = FluidCore::CardLayoutEngine::getStackChevronRect(
                            stack->bounds(), m_state.viewport.originX, m_state.viewport.originY,
                            m_state.viewport.zoom);
                        if (event->x >= chevRect.x && event->x <= chevRect.x + chevRect.w &&
                            event->y >= chevRect.y && event->y <= chevRect.y + chevRect.h) {
                            isHoveringChevron = true;
                            break;
                        }
                    }

                    if (dynamic_cast<const FluidCore::ExcerptCardNode*>(node)) {
                        const auto pillRect = FluidCore::CardLayoutEngine::getExcerptAnchorPillRect(
                            node->bounds(), m_state.viewport.originX, m_state.viewport.originY,
                            m_state.viewport.zoom);
                        if (event->x >= pillRect.x && event->x <= pillRect.x + pillRect.w &&
                            event->y >= pillRect.y && event->y <= pillRect.y + pillRect.h) {
                            newHoveredId = node->id();
                            break;
                        }
                    }
                }
            }

            if (newHoveredId != m_state.hoveredAnchorCardId) {
                m_state.hoveredAnchorCardId = newHoveredId;
                if (m_area && GTK_IS_WIDGET(m_area)) {
                    gtk_widget_queue_draw(m_area);
                }
            }

            if (!m_state.hoveredAnchorCardId.empty() || isHoveringChevron) {
                GdkCursor* pointerCursor = gdk_cursor_new_for_display(display, GDK_HAND2);
                gdk_window_set_cursor(win, pointerCursor);
                if (pointerCursor)
                    g_object_unref(pointerCursor);
            } else if (WorkspaceInteraction::minimapHitTest(m_state, event->x, event->y, alloc.width,
                                                            alloc.height)) {
                GdkCursor* pointerCursor = gdk_cursor_new_for_display(display, GDK_HAND2);
                gdk_window_set_cursor(win, pointerCursor);
                if (pointerCursor)
                    g_object_unref(pointerCursor);
            } else if (m_state.inking.currentTool != "pen" &&
                       m_state.inking.currentTool != "highlighter" &&
                       m_state.inking.currentTool != "eraser" &&
                       m_state.inking.currentTool != "connector") {
                const FluidCore::Point wPt = screenToWorld(event->x, event->y);
                const auto* hitNode = WorkspaceInteraction::hitTestNodeAtWorldPoint(m_api, wPt);
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
    }

    return FALSE;
}

gboolean WorkspaceView::onKeyPress(GdkEventKey* event) {
    if (!m_area || !GTK_IS_WIDGET(m_area)) return FALSE;
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
            setMinimapVisible(!m_state.showMinimap);
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
        if (m_area && GTK_IS_WIDGET(m_area)) {
            GdkWindow* win = gtk_widget_get_window(m_area);
            if (win) {
                GtkWidget* toplevel = gtk_widget_get_toplevel(m_area);
                if (toplevel && GTK_IS_WINDOW(toplevel)) {
                    GtkWidget* focusWidget = gtk_window_get_focus(GTK_WINDOW(toplevel));
                    if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
                        return FALSE;
                    }
                }
            }
        }

        if (m_state.selectedEdgeId.has_value()) {
            m_api.removeEdge(*m_state.selectedEdgeId);
            m_state.selectedEdgeId.reset();
            if (m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
            return TRUE;
        }

        if (m_state.selectedNodeId.has_value()) {
            m_api.removeNode(*m_state.selectedNodeId);
            m_state.selectedNodeId.reset();
            if (m_area && GTK_IS_WIDGET(m_area)) {
                gtk_widget_queue_draw(m_area);
            }
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

} // namespace FluidCoreApp
