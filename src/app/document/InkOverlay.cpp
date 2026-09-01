#include "document/InkOverlay.h"
#include "document/DamageRect.h"
#include "document/DocumentPane.h"
#include "document/SqueezeRenderHelper.h"
#include "undo/AnnotationCommands.h"
#include "workspace/ExcerptPayload.h"

#include <algorithm>
#include <cmath>

namespace FluidCoreApp {
namespace {

constexpr double kPageMargin = 16.0;

StrokeStabilizer::Point2D evalCubicBezier(const StrokeStabilizer::Point2D& b0,
                                          const StrokeStabilizer::Point2D& b1,
                                          const StrokeStabilizer::Point2D& b2,
                                          const StrokeStabilizer::Point2D& b3, double t) {
    const double u = 1.0 - t;
    const double tt = t * t;
    const double uu = u * u;
    const double uuu = uu * u;
    const double ttt = tt * t;

    return {uuu * b0.x + 3.0 * uu * t * b1.x + 3.0 * u * tt * b2.x + ttt * b3.x,
            uuu * b0.y + 3.0 * uu * t * b1.y + 3.0 * u * tt * b2.y + ttt * b3.y};
}

double distSqPointToSegment(double px, double py, double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-6) {
        const double dpx = px - x1;
        const double dpy = py - y1;
        return dpx * dpx + dpy * dpy;
    }
    const double t = std::clamp(((px - x1) * dx + (py - y1) * dy) / lenSq, 0.0, 1.0);
    const double projX = x1 + t * dx;
    const double projY = y1 + t * dy;
    const double dpx = px - projX;
    const double dpy = py - projY;
    return dpx * dpx + dpy * dpy;
}

bool strokeIntersectsEraser(const FluidCore::Stroke& stroke,
                            const std::vector<StrokeStabilizer::StabilizedSample>& eraserSamples,
                            double eraserRadius) {
    if (stroke.points.empty() || eraserSamples.empty()) {
        return false;
    }

    // Fast AABB rejection
    double sMinX = stroke.points[0].x, sMaxX = stroke.points[0].x;
    double sMinY = stroke.points[0].y, sMaxY = stroke.points[0].y;
    for (const auto& pt : stroke.points) {
        sMinX = std::min(sMinX, pt.x);
        sMaxX = std::max(sMaxX, pt.x);
        sMinY = std::min(sMinY, pt.y);
        sMaxY = std::max(sMaxY, pt.y);
    }

    double eMinX = eraserSamples[0].point.x, eMaxX = eraserSamples[0].point.x;
    double eMinY = eraserSamples[0].point.y, eMaxY = eraserSamples[0].point.y;
    for (const auto& sm : eraserSamples) {
        eMinX = std::min(eMinX, sm.point.x);
        eMaxX = std::max(eMaxX, sm.point.x);
        eMinY = std::min(eMinY, sm.point.y);
        eMaxY = std::max(eMaxY, sm.point.y);
    }

    const double pad = stroke.width + eraserRadius;
    if (sMaxX + pad < eMinX || sMinX - pad > eMaxX || sMaxY + pad < eMinY || sMinY - pad > eMaxY) {
        return false;
    }

    const double thresholdSq = (pad * 0.5 + 4.0) * (pad * 0.5 + 4.0);
    for (const auto& sm : eraserSamples) {
        for (std::size_t i = 0; i + 1 < stroke.points.size(); ++i) {
            if (distSqPointToSegment(sm.point.x, sm.point.y, stroke.points[i].x, stroke.points[i].y,
                                     stroke.points[i + 1].x,
                                     stroke.points[i + 1].y) <= thresholdSq) {
                return true;
            }
        }
        if (stroke.points.size() == 1) {
            const double dx = sm.point.x - stroke.points[0].x;
            const double dy = sm.point.y - stroke.points[0].y;
            if (dx * dx + dy * dy <= thresholdSq) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

InkOverlay::InkOverlay(DocumentPane& pane, FluidCore::AnnotationStore& store)
    : m_pane(pane), m_annotationStore(store) {
    m_widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_widget, static_cast<int>(m_pane.layoutWidth()),
                                static_cast<int>(m_pane.layoutHeight()));

    gtk_widget_add_events(m_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
                                        GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(m_widget, "draw", G_CALLBACK(InkOverlay::drawCallback), this);
    g_signal_connect(m_widget, "button-press-event", G_CALLBACK(InkOverlay::buttonPressCallback),
                     this);
    g_signal_connect(m_widget, "motion-notify-event", G_CALLBACK(InkOverlay::motionNotifyCallback),
                     this);
    g_signal_connect(m_widget, "button-release-event",
                     G_CALLBACK(InkOverlay::buttonReleaseCallback), this);
    g_signal_connect(m_widget, "drag-data-get", G_CALLBACK(InkOverlay::dragDataGetCallback), this);
    g_signal_connect(m_widget, "drag-end", G_CALLBACK(InkOverlay::dragEndCallback), this);
    g_signal_connect(m_widget, "enter-notify-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer data) -> gboolean {
                         static_cast<InkOverlay*>(data)->updateCursor();
                         return FALSE;
                     }),
                     this);
}

InkOverlay::~InkOverlay() = default;

void InkOverlay::setTool(const std::string& tool) {
    m_currentTool = tool;
    if (tool == "highlighter") {
        m_currentColor = 0xFFFF00; // Yellow highlighter
        m_currentWidth = 14.0;
    } else if (tool == "pen") {
        m_currentColor = 0x000000; // Black ink pen
        m_currentWidth = 2.0;
    } else if (tool == "eraser") {
        m_currentWidth = 20.0;
    }
    updateCursor();
}

void InkOverlay::updateCursor() {
    if (!m_widget) {
        return;
    }
    GdkWindow* window = gtk_widget_get_window(m_widget);
    if (!window) {
        return;
    }
    GdkDisplay* display = gtk_widget_get_display(m_widget);
    GdkCursor* cursor = nullptr;

    if (m_currentTool == "select" || m_currentTool == "text") {
        cursor = gdk_cursor_new_from_name(display, "text");
        if (!cursor) {
            cursor = gdk_cursor_new_for_display(display, GDK_XTERM);
        }
    } else if (m_currentTool == "crop" || m_currentTool == "rect_select" ||
               m_currentTool == "eraser") {
        cursor = gdk_cursor_new_from_name(display, "crosshair");
        if (!cursor) {
            cursor = gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
        }
    } else {
        cursor = gdk_cursor_new_from_name(display, "default");
    }

    gdk_window_set_cursor(window, cursor);
    if (cursor) {
        g_object_unref(cursor);
    }
}

void InkOverlay::clearSelection() {
    if (m_selectionState.hasSelection) {
        invalidateSelection(m_selectionState);
        m_selectionState.clear();
    }
}

void InkOverlay::clearCropSelection() {
    if (m_cropSelectionState.hasSelection) {
        invalidateCropSelection();
        m_cropSelectionState = CropSelectionState{};
    }
}

void InkOverlay::invalidateCropSelection() {
    if (m_widget) {
        gtk_widget_queue_draw(m_widget);
    }
}

bool InkOverlay::isPointInsideCropSelection(std::size_t pageIndex, double xp, double yp) const {
    if (!m_cropSelectionState.hasSelection || m_cropSelectionState.pageIndex != pageIndex) {
        return false;
    }
    const auto& r = m_cropSelectionState.rectPt;
    return (xp >= r.x && xp <= r.x + r.w && yp >= r.y && yp <= r.y + r.h);
}

bool InkOverlay::copySelection() {
    if (!m_selectionState.hasSelection || m_selectionState.fullText.empty()) {
        return false;
    }
    return TextSelectionService::copyToClipboard(m_selectionState.fullText);
}

void InkOverlay::invalidateSelection(const FluidCore::MultiPageSelectionState& state) {
    if (!state.hasSelection || state.pages.empty()) {
        return;
    }

    if (m_pane.isSqueezed()) {
        gtk_widget_queue_draw(m_widget);
        return;
    }

    const auto& pages = m_pane.pages();
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);

    for (const auto& pSel : state.pages) {
        if (pSel.pageIndex < pages.size()) {
            const auto damage = FluidCore::TextSelection::computePageDamage(
                pSel, pageX, pages[pSel.pageIndex].y, 4.0);
            if (!damage.isEmpty()) {
                gtk_widget_queue_draw_area(m_widget, damage.x, damage.y, damage.width,
                                           damage.height);
            }
        }
    }
}

void InkOverlay::invalidateStroke(const FluidCore::Stroke& stroke) {
    if (stroke.points.empty()) {
        return;
    }

    if (m_pane.isSqueezed()) {
        gtk_widget_queue_draw(m_widget);
        return;
    }

    const auto& pages = m_pane.pages();
    if (stroke.pageIndex >= pages.size()) {
        return;
    }

    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);
    const double pageY = pages[stroke.pageIndex].y;

    double minX = stroke.points[0].x, maxX = stroke.points[0].x;
    double minY = stroke.points[0].y, maxY = stroke.points[0].y;
    for (const auto& pt : stroke.points) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }

    const double pad = std::max(8.0, stroke.width * 2.0);
    const int rx = std::max(0, static_cast<int>(std::floor(pageX + minX - pad)));
    const int ry = std::max(0, static_cast<int>(std::floor(pageY + minY - pad)));
    const int rw = static_cast<int>(std::ceil(maxX - minX + 2.0 * pad));
    const int rh = static_cast<int>(std::ceil(maxY - minY + 2.0 * pad));

    gtk_widget_queue_draw_area(m_widget, rx, ry, rw, rh);
}

void InkOverlay::invalidatePage(std::size_t pageIdx) {
    const auto& pages = m_pane.pages();
    if (pageIdx >= pages.size()) {
        return;
    }

    if (m_pane.isSqueezed()) {
        gtk_widget_queue_draw(m_widget);
        return;
    }

    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);
    const auto& layout = pages[pageIdx];

    gtk_widget_queue_draw_area(
        m_widget, static_cast<int>(std::floor(pageX)), static_cast<int>(std::floor(layout.y)),
        static_cast<int>(std::ceil(layout.width)), static_cast<int>(std::ceil(layout.height)));
}

void InkOverlay::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    static_cast<InkOverlay*>(userData)->draw(cr);
}

gboolean InkOverlay::buttonPressCallback(GtkWidget*, GdkEventButton* event, gpointer userData) {
    return static_cast<InkOverlay*>(userData)->onButtonPress(event);
}

gboolean InkOverlay::motionNotifyCallback(GtkWidget*, GdkEventMotion* event, gpointer userData) {
    return static_cast<InkOverlay*>(userData)->onMotionNotify(event);
}

gboolean InkOverlay::buttonReleaseCallback(GtkWidget*, GdkEventButton* event, gpointer userData) {
    return static_cast<InkOverlay*>(userData)->onButtonRelease(event);
}

gboolean InkOverlay::onButtonPress(GdkEventButton* event) {
    m_pane.notifyActivated();

    if (event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    GdkDevice* device = gdk_event_get_source_device(reinterpret_cast<GdkEvent*>(event));
    const GdkInputSource source = device ? gdk_device_get_source(device) : GDK_SOURCE_MOUSE;
    if (source == GDK_SOURCE_TOUCHSCREEN) {
        return FALSE;
    }

    const double zoom = m_pane.zoom();
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double unscaledWidth = allocation.width / zoom;
    const double pageX = kPageMargin + std::max(0.0, (unscaledWidth - m_pane.layoutWidth()) / 2.0);

    const auto& pages = m_pane.pages();
    const double screenX = event->x / zoom;
    const double docY = m_pane.screenYToDoc(event->y);

    if (m_currentTool == "crop" || m_currentTool == "rect_select") {
        // Visual diagram crop selection mode
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const auto& layout = pages[i];
            if (docY >= layout.y && docY <= layout.y + layout.height && screenX >= pageX &&
                screenX <= pageX + layout.width) {
                const double xp = screenX - pageX;
                const double yp = docY - layout.y;

                if (m_cropSelectionState.hasSelection && isPointInsideCropSelection(i, xp, yp)) {
                    // Clicked inside existing crop selection -> potential drag-out excerpt
                    m_isPotentialExcerptDrag = true;
                    m_pressScreenX = event->x;
                    m_pressScreenY = event->y;
                    m_dragSourcePageIndex = i;
                    return TRUE;
                }

                clearCropSelection();
                clearSelection();

                m_isSelectingCrop = true;
                m_dragStartPageIndex = i;
                m_cropDragStartPoint = {xp, yp};

                m_cropSelectionState.hasSelection = true;
                m_cropSelectionState.pageIndex = i;
                m_cropSelectionState.rectPt = {xp, yp, 0.0, 0.0};
                m_cropSelectionState.normRect = {xp / layout.width, yp / layout.height, 0.0, 0.0};

                gtk_widget_queue_draw(m_widget);
                return TRUE;
            }
        }
        return FALSE;
    }

    if (m_currentTool == "select" || m_currentTool == "text") {
        // Text selection mode
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const auto& layout = pages[i];
            if (docY >= layout.y && docY <= layout.y + layout.height && screenX >= pageX &&
                screenX <= pageX + layout.width) {
                const double xp = screenX - pageX;
                const double yp = docY - layout.y;

                if (m_selectionState.hasSelection && isPointInsideSelection(i, xp, yp)) {
                    // Clicked inside existing selection -> potential drag-out excerpt
                    m_isPotentialExcerptDrag = true;
                    m_pressScreenX = event->x;
                    m_pressScreenY = event->y;
                    m_dragSourcePageIndex = i;
                    return TRUE;
                }

                if (m_selectionState.hasSelection) {
                    invalidateSelection(m_selectionState);
                    m_selectionState.clear();
                }

                clearCropSelection();

                m_isSelectingText = true;
                m_dragStartPageIndex = i;
                m_dragStartPoint = {xp, yp};

                m_textSelectionService.updateLiveDrag(pages, i, m_dragStartPoint, i, {xp, yp},
                                                      m_selectionState);
                invalidateSelection(m_selectionState);
                return TRUE;
            }
        }
        return FALSE;
    }

    // Inking mode: clear any existing text and crop selections when starting a stroke
    if (m_selectionState.hasSelection) {
        clearSelection();
    }
    if (m_cropSelectionState.hasSelection) {
        clearCropSelection();
    }

    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& layout = pages[i];
        if (docY >= layout.y && docY <= layout.y + layout.height && screenX >= pageX &&
            screenX <= pageX + layout.width) {
            m_isDrawing = true;
            m_activePageIndex = i;

            gdouble pressure = 1.0;
            if (!gdk_event_get_axis(reinterpret_cast<GdkEvent*>(event), GDK_AXIS_PRESSURE,
                                    &pressure) ||
                pressure <= 0.0) {
                pressure = 1.0;
            }

            m_lastPressure = pressure;
            m_activeStroke = FluidCore::Stroke{};
            m_activeStroke.tool = (source == GDK_SOURCE_ERASER) ? "eraser" : m_currentTool;
            m_activeStroke.color = m_currentColor;
            m_activeStroke.width = m_currentWidth;
            m_activeStroke.timestamp = static_cast<std::uint64_t>(event->time);

            const double xp = screenX - pageX;
            const double yp = docY - layout.y;

            m_stabilizer.beginStroke({xp, yp}, pressure, static_cast<std::uint64_t>(event->time),
                                     StabilizerMode::Smooth);
            m_activeBezierSegments.clear();
            m_wetTip = {xp, yp};
            m_hasWetSegment = false;

            if (m_activeStroke.tool == "eraser") {
                const auto existingStrokes = m_annotationStore.strokesForPage(i);
                const auto& samples = m_stabilizer.rawSamples();
                for (const auto& s : existingStrokes) {
                    if (strokeIntersectsEraser(s, samples, 24.0)) {
                        invalidateStroke(s);
                        m_pane.undoStack().pushAndExecute(
                            std::make_unique<FluidCore::RemoveStrokeCommand>(m_annotationStore, i,
                                                                             s));
                    }
                }
            }

            gtk_widget_queue_draw(m_widget);
            return TRUE;
        }
    }

    return FALSE;
}

gboolean InkOverlay::onMotionNotify(GdkEventMotion* event) {
    if (m_isPotentialExcerptDrag) {
        const double dist = std::hypot(event->x - m_pressScreenX, event->y - m_pressScreenY);
        if (dist >= 4.0) {
            m_isPotentialExcerptDrag = false;
            static const GtkTargetEntry dragTargets[] = {
                {const_cast<gchar*>("application/x-fluid-excerpt"), GTK_TARGET_SAME_APP, 0},
                {const_cast<gchar*>("text/plain"), 0, 1}};
            GtkTargetList* targetList = gtk_target_list_new(dragTargets, G_N_ELEMENTS(dragTargets));
            GdkDragContext* context = gtk_drag_begin_with_coordinates(
                m_widget, targetList, GDK_ACTION_COPY, 1, reinterpret_cast<GdkEvent*>(event),
                static_cast<gint>(event->x), static_cast<gint>(event->y));
            gtk_target_list_unref(targetList);
            if (context) {
                gtk_drag_set_icon_name(context, "edit-copy", 0, 0);
            }
            return TRUE;
        }
        return TRUE;
    }

    const double zoom = m_pane.zoom();
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double unscaledWidth = allocation.width / zoom;
    const double pageX = kPageMargin + std::max(0.0, (unscaledWidth - m_pane.layoutWidth()) / 2.0);
    const auto& pages = m_pane.pages();
    const double screenX = event->x / zoom;
    const double docY = m_pane.screenYToDoc(event->y);

    if (m_isSelectingCrop) {
        if (m_dragStartPageIndex < pages.size()) {
            const auto& layout = pages[m_dragStartPageIndex];
            const double xp = std::clamp(screenX - pageX, 0.0, layout.width);
            const double yp = std::clamp(docY - layout.y, 0.0, layout.height);
            double minX = std::min(m_cropDragStartPoint.x, xp);
            double minY = std::min(m_cropDragStartPoint.y, yp);
            double maxX = std::max(m_cropDragStartPoint.x, xp);
            double maxY = std::max(m_cropDragStartPoint.y, yp);

            m_cropSelectionState.hasSelection = true;
            m_cropSelectionState.pageIndex = m_dragStartPageIndex;
            m_cropSelectionState.rectPt = {minX, minY, maxX - minX, maxY - minY};
            m_cropSelectionState.normRect = {minX / layout.width, minY / layout.height,
                                             (maxX - minX) / layout.width,
                                             (maxY - minY) / layout.height};
            gtk_widget_queue_draw(m_widget);
            return TRUE;
        }
    }

    if (m_isSelectingText) {
        if (pages.empty()) {
            return FALSE;
        }

        std::size_t currPage = 0;
        if (docY < pages.front().y) {
            currPage = 0;
        } else if (docY >= pages.back().y + pages.back().height) {
            currPage = pages.size() - 1;
        } else {
            for (std::size_t i = 0; i < pages.size(); ++i) {
                if (docY >= pages[i].y && docY <= pages[i].y + pages[i].height + 12.0) {
                    currPage = i;
                    break;
                }
            }
        }

        const auto& layout = pages[currPage];
        const double xp = std::clamp(screenX - pageX, 0.0, layout.width);
        const double yp = std::clamp(docY - layout.y, 0.0, layout.height);

        FluidCore::MultiPageSelectionState oldState = m_selectionState;
        m_textSelectionService.updateLiveDrag(pages, m_dragStartPageIndex, m_dragStartPoint,
                                              currPage, {xp, yp}, m_selectionState);

        invalidateSelection(oldState);
        invalidateSelection(m_selectionState);
        return TRUE;
    }

    if (!m_isDrawing) {
        return FALSE;
    }

    if (m_activePageIndex >= pages.size()) {
        return FALSE;
    }

    const auto& layout = pages[m_activePageIndex];
    const double xp = screenX - pageX;
    const double yp = docY - layout.y;

    if (m_activeStroke.tool == "eraser") {
        m_stabilizer.pushPoint({xp, yp}, 1.0, static_cast<std::uint64_t>(event->time));
        const auto existingStrokes = m_annotationStore.strokesForPage(m_activePageIndex);
        const auto& samples = m_stabilizer.rawSamples();
        bool erasedAny = false;
        for (const auto& s : existingStrokes) {
            if (strokeIntersectsEraser(s, samples, 24.0)) {
                invalidateStroke(s);
                m_pane.undoStack().pushAndExecute(std::make_unique<FluidCore::RemoveStrokeCommand>(
                    m_annotationStore, m_activePageIndex, s));
                erasedAny = true;
            }
        }
        if (erasedAny) {
            gtk_widget_queue_draw(m_widget);
        }
        return TRUE;
    }

    gdouble pressure = 1.0;
    if (!gdk_event_get_axis(reinterpret_cast<GdkEvent*>(event), GDK_AXIS_PRESSURE, &pressure) ||
        pressure <= 0.0) {
        pressure = 1.0;
    }

    auto pushResult =
        m_stabilizer.pushPoint({xp, yp}, pressure, static_cast<std::uint64_t>(event->time));

    for (const auto& seg : pushResult.newlyCommitted) {
        m_activeBezierSegments.push_back(seg);
    }

    m_wetTip = pushResult.wetTip;
    m_hasWetSegment = pushResult.hasWetSegment;

    gtk_widget_queue_draw(m_widget);
    m_lastPressure = pressure;
    return TRUE;
}

gboolean InkOverlay::onButtonRelease(GdkEventButton* event) {
    if (m_isPotentialExcerptDrag) {
        m_isPotentialExcerptDrag = false;
        clearSelection();
        clearCropSelection();
        return TRUE;
    }

    if (m_isSelectingCrop) {
        m_isSelectingCrop = false;
        if (m_cropSelectionState.rectPt.w < 6.0 || m_cropSelectionState.rectPt.h < 6.0) {
            clearCropSelection();
        }
        gtk_widget_queue_draw(m_widget);
        return TRUE;
    }

    if (m_isSelectingText) {
        m_isSelectingText = false;
        m_textSelectionService.finalizeSelection(m_pane.pages(), m_selectionState);
        if (m_selectionState.fullText.empty()) {
            bool hasLines = false;
            for (const auto& pSel : m_selectionState.pages) {
                if (!pSel.lineRects.empty()) {
                    hasLines = true;
                    break;
                }
            }
            if (!hasLines) {
                clearSelection();
            }
        }
        gtk_widget_queue_draw(m_widget);
        return TRUE;
    }

    if (!m_isDrawing || event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    m_isDrawing = false;
    const auto& pages = m_pane.pages();
    if (m_activePageIndex < pages.size()) {
        auto tailSegs = m_stabilizer.endStroke();
        for (const auto& seg : tailSegs) {
            m_activeBezierSegments.push_back(seg);
        }
        gtk_widget_queue_draw(m_widget);

        const auto& samples = m_stabilizer.rawSamples();
        if (m_activeStroke.tool == "eraser") {
            const auto existingStrokes = m_annotationStore.strokesForPage(m_activePageIndex);
            std::vector<FluidCore::Stroke> erasedStrokes;
            for (const auto& s : existingStrokes) {
                if (strokeIntersectsEraser(s, samples,
                                           std::max(12.0, m_activeStroke.width * 4.0))) {
                    erasedStrokes.push_back(s);
                }
            }

            if (!erasedStrokes.empty()) {
                if (erasedStrokes.size() == 1) {
                    invalidateStroke(erasedStrokes[0]);
                    m_pane.undoStack().pushAndExecute(
                        std::make_unique<FluidCore::RemoveStrokeCommand>(
                            m_annotationStore, m_activePageIndex, erasedStrokes[0]));
                } else {
                    auto compound = std::make_unique<FluidCore::CompoundCommand>("Erase Strokes");
                    for (const auto& s : erasedStrokes) {
                        invalidateStroke(s);
                        compound->addCommand(std::make_unique<FluidCore::RemoveStrokeCommand>(
                            m_annotationStore, m_activePageIndex, s));
                    }
                    m_pane.undoStack().pushAndExecute(std::move(compound));
                }
            }
        } else if (!samples.empty()) {
            m_activeStroke.points.clear();
            m_activeStroke.pressures.clear();
            m_activeStroke.points.reserve(samples.size());
            if (samples.size() > 1) {
                m_activeStroke.pressures.reserve(samples.size() - 1);
            }

            for (std::size_t i = 0; i < samples.size(); ++i) {
                m_activeStroke.points.push_back({samples[i].point.x, samples[i].point.y});
                if (i + 1 < samples.size()) {
                    m_activeStroke.pressures.push_back(samples[i].pressure);
                }
            }

            m_pane.undoStack().pushAndExecute(std::make_unique<FluidCore::AddStrokeCommand>(
                m_annotationStore, m_activePageIndex, std::move(m_activeStroke)));
        }
    }

    m_activeStroke = FluidCore::Stroke{};
    m_activeBezierSegments.clear();
    m_hasWetSegment = false;
    gtk_widget_queue_draw(m_widget);
    return TRUE;
}

void InkOverlay::renderBezierSegment(cairo_t* cr, const StrokeStabilizer::BezierSegment& seg,
                                     double baseWidth) const {
    constexpr int kSubdivisions = 3;
    StrokeStabilizer::Point2D prevPt = seg.p0;
    double prevP = seg.pressure0;

    for (int k = 1; k <= kSubdivisions; ++k) {
        const double t = static_cast<double>(k) / kSubdivisions;
        const auto currPt = evalCubicBezier(seg.p0, seg.p1, seg.p2, seg.p3, t);
        const double currP = (1.0 - t) * seg.pressure0 + t * seg.pressure1;
        const double segWidth = std::max(0.5, baseWidth * 0.5 * (prevP + currP));

        cairo_set_line_width(cr, segWidth);
        cairo_move_to(cr, prevPt.x, prevPt.y);
        cairo_line_to(cr, currPt.x, currPt.y);
        cairo_stroke(cr);

        prevPt = currPt;
        prevP = currP;
    }
}

void InkOverlay::renderActiveLiveStroke(cairo_t* cr) const {
    if (m_activeBezierSegments.empty() && !m_hasWetSegment) {
        if (!m_stabilizer.rawSamples().empty()) {
            const auto& p0 = m_stabilizer.rawSamples().front().point;
            const double radius = std::max(0.5, m_activeStroke.width * m_lastPressure * 0.5);
            cairo_arc(cr, p0.x, p0.y, radius, 0.0, 2.0 * M_PI);
            cairo_fill(cr);
        }
        return;
    }

    for (const auto& seg : m_activeBezierSegments) {
        renderBezierSegment(cr, seg, m_activeStroke.width);
    }

    if (m_hasWetSegment) {
        StrokeStabilizer::Point2D lastEnd = m_wetTip;
        if (!m_activeBezierSegments.empty()) {
            lastEnd = m_activeBezierSegments.back().p3;
        } else if (!m_stabilizer.rawSamples().empty()) {
            lastEnd = m_stabilizer.rawSamples().front().point;
        }

        const double segWidth = std::max(0.5, m_activeStroke.width * m_lastPressure);
        cairo_set_line_width(cr, segWidth);
        cairo_move_to(cr, lastEnd.x, lastEnd.y);
        cairo_line_to(cr, m_wetTip.x, m_wetTip.y);
        cairo_stroke(cr);
    }
}

void InkOverlay::renderTextSelection(cairo_t* cr, std::size_t pageIndex) const {
    if (!m_selectionState.hasSelection) {
        return;
    }

    for (const auto& pageSel : m_selectionState.pages) {
        if (pageSel.pageIndex == pageIndex) {
            cairo_save(cr);
            for (const auto& line : pageSel.lineRects) {
                cairo_set_source_rgba(cr, 0.15, 0.45, 0.90, 0.30);
                cairo_rectangle(cr, line.x0, line.y0, line.width(), line.height());
                cairo_fill_preserve(cr);

                cairo_set_source_rgba(cr, 0.15, 0.45, 0.90, 0.75);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
        }
    }
}

void InkOverlay::renderStroke(cairo_t* cr, const FluidCore::Stroke& stroke) const {
    if (stroke.points.empty()) {
        return;
    }

    const double r = ((stroke.color >> 16) & 0xFF) / 255.0;
    const double g = ((stroke.color >> 8) & 0xFF) / 255.0;
    const double b = (stroke.color & 0xFF) / 255.0;
    const bool isHighlighter = (stroke.tool == "highlighter");

    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    if (isHighlighter) {
        cairo_push_group(cr);
        cairo_set_source_rgb(cr, r, g, b);
    } else {
        cairo_set_source_rgb(cr, r, g, b);
    }

    if (stroke.points.size() == 1) {
        const double radius = std::max(0.5, stroke.width / 2.0);
        cairo_arc(cr, stroke.points[0].x, stroke.points[0].y, radius, 0.0, 2.0 * M_PI);
        cairo_fill(cr);
    } else if (stroke.points.size() == 2) {
        const double p0 = stroke.pressures.empty() ? 1.0 : stroke.pressures[0];
        cairo_set_line_width(cr, std::max(0.5, stroke.width * p0));
        cairo_move_to(cr, stroke.points[0].x, stroke.points[0].y);
        cairo_line_to(cr, stroke.points[1].x, stroke.points[1].y);
        cairo_stroke(cr);
    } else {
        const std::size_t n = stroke.points.size();
        for (std::size_t i = 0; i < n - 1; ++i) {
            StrokeStabilizer::Point2D p0 =
                (i == 0)
                    ? StrokeStabilizer::Point2D{2.0 * stroke.points[0].x - stroke.points[1].x,
                                                2.0 * stroke.points[0].y - stroke.points[1].y}
                    : StrokeStabilizer::Point2D{stroke.points[i - 1].x, stroke.points[i - 1].y};

            StrokeStabilizer::Point2D p1 = {stroke.points[i].x, stroke.points[i].y};
            StrokeStabilizer::Point2D p2 = {stroke.points[i + 1].x, stroke.points[i + 1].y};

            StrokeStabilizer::Point2D p3 =
                (i + 2 < n)
                    ? StrokeStabilizer::Point2D{stroke.points[i + 2].x, stroke.points[i + 2].y}
                    : StrokeStabilizer::Point2D{
                          2.0 * stroke.points[n - 1].x - stroke.points[n - 2].x,
                          2.0 * stroke.points[n - 1].y - stroke.points[n - 2].y};

            const double pr1 = (i < stroke.pressures.size()) ? stroke.pressures[i] : 1.0;
            const double pr2 = (i + 1 < stroke.pressures.size()) ? stroke.pressures[i + 1] : pr1;

            const auto seg =
                StrokeStabilizer::centripetalCatmullRomToBezier(p0, p1, p2, p3, pr1, pr2);
            renderBezierSegment(cr, seg, stroke.width);
        }
    }

    if (isHighlighter) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.5);
    }

    cairo_restore(cr);
}

void InkOverlay::draw(cairo_t* cr) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);

    GdkRectangle clip;
    if (!gdk_cairo_get_clip_rectangle(cr, &clip)) {
        clip.x = 0;
        clip.y = 0;
        clip.width = allocation.width;
        clip.height = allocation.height;
    }

    const double zoom = m_pane.zoom();
    const double clipYStart = clip.y / zoom;
    const double clipYEnd = (clip.y + clip.height) / zoom;
    const double unscaledWidth = allocation.width / zoom;
    const double pageX = kPageMargin + std::max(0.0, (unscaledWidth - m_pane.layoutWidth()) / 2.0);

    cairo_save(cr);
    cairo_scale(cr, zoom, zoom);

    const auto segments = m_pane.squeezeSegments();
    const auto& pages = m_pane.pages();

    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& layout = pages[i];
        auto slices = SqueezeRenderHelper::decomposePage(i, layout.y, layout.height, segments);

        for (const auto& slice : slices) {
            if (slice.screenYEnd < clipYStart || slice.screenYStart > clipYEnd) {
                continue;
            }

            cairo_save(cr);
            // Clip to slice
            cairo_rectangle(cr, pageX, slice.screenYStart, layout.width,
                            slice.screenYEnd - slice.screenYStart);
            cairo_clip(cr);

            // Translate origin to top of page in screen space for this slice
            const double yOffset = slice.screenYStart - slice.pageLocalDocYStart;
            cairo_translate(cr, pageX, yOffset);

            // Render text selection highlight
            renderTextSelection(cr, i);

            // Render visual diagram crop selection
            renderCropSelection(cr, i);

            // Render strokes
            const std::vector<FluidCore::Stroke> pageStrokes = m_annotationStore.strokesForPage(i);
            for (const FluidCore::Stroke& stroke : pageStrokes) {
                renderStroke(cr, stroke);
            }

            // Render active live stroke
            if (m_isDrawing && m_activePageIndex == i && m_activeStroke.tool != "eraser") {
                const double r = ((m_activeStroke.color >> 16) & 0xFF) / 255.0;
                const double g = ((m_activeStroke.color >> 8) & 0xFF) / 255.0;
                const double b = (m_activeStroke.color & 0xFF) / 255.0;
                const bool isHighlighter = (m_activeStroke.tool == "highlighter");

                cairo_save(cr);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

                if (isHighlighter) {
                    cairo_push_group(cr);
                    cairo_set_source_rgb(cr, r, g, b);
                } else {
                    cairo_set_source_rgb(cr, r, g, b);
                }

                renderActiveLiveStroke(cr);

                if (isHighlighter) {
                    cairo_pop_group_to_source(cr);
                    cairo_paint_with_alpha(cr, 0.5);
                }

                cairo_restore(cr);
            }

            cairo_restore(cr);
        }
    }

    cairo_restore(cr);
}

void InkOverlay::renderCropSelection(cairo_t* cr, std::size_t pageIndex) const {
    if (!m_cropSelectionState.hasSelection || m_cropSelectionState.pageIndex != pageIndex) {
        return;
    }

    const auto& r = m_cropSelectionState.rectPt;
    if (r.w <= 0.0 || r.h <= 0.0) {
        return;
    }

    cairo_save(cr);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.18);
    cairo_fill_preserve(cr);

    double dashes[] = {4.0, 4.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_set_source_rgba(cr, 0.05, 0.65, 0.95, 0.95);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    // Render corner control points
    cairo_set_dash(cr, nullptr, 0, 0.0);
    cairo_set_source_rgb(cr, 0.05, 0.65, 0.95);
    cairo_rectangle(cr, r.x - 3.0, r.y - 3.0, 6.0, 6.0);
    cairo_fill(cr);
    cairo_rectangle(cr, r.x + r.w - 3.0, r.y - 3.0, 6.0, 6.0);
    cairo_fill(cr);
    cairo_rectangle(cr, r.x - 3.0, r.y + r.h - 3.0, 6.0, 6.0);
    cairo_fill(cr);
    cairo_rectangle(cr, r.x + r.w - 3.0, r.y + r.h - 3.0, 6.0, 6.0);
    cairo_fill(cr);

    cairo_restore(cr);
}

bool InkOverlay::isPointInsideSelection(std::size_t pageIndex, double xp, double yp) const {
    if (!m_selectionState.hasSelection) {
        return false;
    }
    for (const auto& pageSel : m_selectionState.pages) {
        if (pageSel.pageIndex == pageIndex) {
            for (const auto& r : pageSel.lineRects) {
                if (xp >= r.x0 && xp <= r.x1 && yp >= r.y0 && yp <= r.y1) {
                    return true;
                }
            }
            if (!pageSel.dragBounds.isEmpty() && xp >= pageSel.dragBounds.x0 &&
                xp <= pageSel.dragBounds.x1 && yp >= pageSel.dragBounds.y0 &&
                yp <= pageSel.dragBounds.y1) {
                return true;
            }
        }
    }
    return false;
}

FluidCore::Rectangle InkOverlay::computeNormalizedSelectionBounds(std::size_t pageIndex,
                                                                  double pageWidth,
                                                                  double pageHeight) const {
    if (!m_selectionState.hasSelection || pageWidth <= 0.0 || pageHeight <= 0.0) {
        return {0.0, 0.0, 1.0, 1.0};
    }

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    bool found = false;

    for (const auto& pageSel : m_selectionState.pages) {
        if (pageSel.pageIndex == pageIndex) {
            for (const auto& r : pageSel.lineRects) {
                minX = std::min(minX, r.x0);
                minY = std::min(minY, r.y0);
                maxX = std::max(maxX, r.x1);
                maxY = std::max(maxY, r.y1);
                found = true;
            }
            if (!pageSel.dragBounds.isEmpty()) {
                minX = std::min(minX, pageSel.dragBounds.x0);
                minY = std::min(minY, pageSel.dragBounds.y0);
                maxX = std::max(maxX, pageSel.dragBounds.x1);
                maxY = std::max(maxY, pageSel.dragBounds.y1);
                found = true;
            }
        }
    }

    if (!found) {
        return {0.0, 0.0, 1.0, 1.0};
    }

    const double nx = std::clamp(minX / pageWidth, 0.0, 1.0);
    const double ny = std::clamp(minY / pageHeight, 0.0, 1.0);
    const double nw = std::clamp((maxX - minX) / pageWidth, 0.0, 1.0 - nx);
    const double nh = std::clamp((maxY - minY) / pageHeight, 0.0, 1.0 - ny);

    return {nx, ny, nw, nh};
}

void InkOverlay::dragDataGetCallback(GtkWidget*, GdkDragContext* context, GtkSelectionData* data,
                                     guint info, guint time, gpointer userData) {
    static_cast<InkOverlay*>(userData)->onDragDataGet(context, data, info, time);
}

void InkOverlay::dragEndCallback(GtkWidget*, GdkDragContext* context, gpointer userData) {
    static_cast<InkOverlay*>(userData)->onDragEnd(context);
}

void InkOverlay::onDragDataGet(GdkDragContext*, GtkSelectionData* data, guint info, guint) {
    if (m_cropSelectionState.hasSelection) {
        const auto& pages = m_pane.pages();
        std::size_t cropPage = m_cropSelectionState.pageIndex;
        double pw = (cropPage < pages.size()) ? pages[cropPage].width : 612.0;
        double ph = (cropPage < pages.size()) ? pages[cropPage].height : 792.0;

        // Upstream minimum crop dimension guard (ignore degenerate hairline crops < 8pt)
        if (m_cropSelectionState.normRect.w * pw < 8.0 ||
            m_cropSelectionState.normRect.h * ph < 8.0) {
            return;
        }

        FluidCore::ExcerptDropPayload payload;
        payload.sourceDocId = m_pane.pdfPath().empty() ? m_pane.docId() : m_pane.pdfPath();
        payload.sourcePageNo = cropPage;
        payload.sourceNormalizedRect = m_cropSelectionState.normRect;
        payload.sourcePageWidth = pw;
        payload.sourcePageHeight = ph;
        payload.textSnippet = "";
        payload.isImageExcerpt = true;
        payload.color = {168, 85, 247, 255}; // Radiant diagram accent

        if (info == 0) { // application/x-fluid-excerpt
            std::string serialized = FluidCore::serializeExcerptPayload(payload);
            gtk_selection_data_set(data,
                                   gdk_atom_intern_static_string("application/x-fluid-excerpt"), 8,
                                   reinterpret_cast<const guchar*>(serialized.data()),
                                   static_cast<gint>(serialized.size()));
        } else if (info == 1) { // text/plain
            gtk_selection_data_set_text(data, "[Visual Diagram Crop]", -1);
        }
        return;
    }

    if (!m_selectionState.hasSelection) {
        return;
    }

    const auto& pages = m_pane.pages();
    std::size_t pageNo =
        m_selectionState.pages.empty() ? 0 : m_selectionState.pages.front().pageIndex;
    double pw = (pageNo < pages.size()) ? pages[pageNo].width : 612.0;
    double ph = (pageNo < pages.size()) ? pages[pageNo].height : 792.0;

    FluidCore::Rectangle normRect = computeNormalizedSelectionBounds(pageNo, pw, ph);

    FluidCore::ExcerptDropPayload payload;
    payload.sourceDocId = m_pane.pdfPath().empty() ? m_pane.docId() : m_pane.pdfPath();
    payload.sourcePageNo = pageNo;
    payload.sourceNormalizedRect = normRect;
    payload.sourcePageWidth = pw;
    payload.sourcePageHeight = ph;
    payload.textSnippet = m_selectionState.fullText;
    payload.isImageExcerpt = false;
    payload.color = {255, 220, 0, 255};

    if (info == 0) { // application/x-fluid-excerpt
        std::string serialized = FluidCore::serializeExcerptPayload(payload);
        gtk_selection_data_set(data, gdk_atom_intern_static_string("application/x-fluid-excerpt"),
                               8, reinterpret_cast<const guchar*>(serialized.data()),
                               static_cast<gint>(serialized.size()));
    } else if (info == 1) { // text/plain
        gtk_selection_data_set_text(data, m_selectionState.fullText.c_str(), -1);
    }
}

void InkOverlay::onDragEnd(GdkDragContext*) {
    m_isPotentialExcerptDrag = false;
}

} // namespace FluidCoreApp
