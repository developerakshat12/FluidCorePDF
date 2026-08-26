#include "InkOverlay.h"
#include "DocumentPane.h"

#include <algorithm>
#include <cmath>

namespace FluidCoreApp {
namespace {

constexpr double kPageMargin = 12.0;

} // namespace

InkOverlay::InkOverlay(DocumentPane& pane, FluidCore::AnnotationStore& store)
    : m_pane(pane), m_annotationStore(store) {
    m_widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_widget, static_cast<int>(m_pane.layoutWidth()),
                                static_cast<int>(m_pane.layoutHeight()));

    gtk_widget_add_events(m_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK);

    g_signal_connect(m_widget, "draw", G_CALLBACK(InkOverlay::drawCallback), this);
    g_signal_connect(m_widget, "button-press-event", G_CALLBACK(InkOverlay::buttonPressCallback),
                     this);
    g_signal_connect(m_widget, "motion-notify-event", G_CALLBACK(InkOverlay::motionNotifyCallback),
                     this);
    g_signal_connect(m_widget, "button-release-event",
                     G_CALLBACK(InkOverlay::buttonReleaseCallback), this);
}

InkOverlay::~InkOverlay() = default;

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
    if (event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    GdkDevice* device = gdk_event_get_source_device(reinterpret_cast<GdkEvent*>(event));
    const GdkInputSource source = device ? gdk_device_get_source(device) : GDK_SOURCE_MOUSE;
    if (source == GDK_SOURCE_TOUCHSCREEN) {
        // Touch gestures reserved for multi-touch panning / squeeze in M2+
        return FALSE;
    }

    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);

    const auto& pages = m_pane.pages();
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& layout = pages[i];
        if (event->y >= layout.y && event->y <= layout.y + layout.height) {
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

            const double xp = event->x - pageX;
            const double yp = event->y - layout.y;
            m_activeStroke.points.push_back({xp, yp});

            gtk_widget_queue_draw(m_widget);
            return TRUE;
        }
    }

    return FALSE;
}

gboolean InkOverlay::onMotionNotify(GdkEventMotion* event) {
    if (!m_isDrawing) {
        return FALSE;
    }

    GdkDevice* device = gdk_event_get_source_device(reinterpret_cast<GdkEvent*>(event));
    const GdkInputSource source = device ? gdk_device_get_source(device) : GDK_SOURCE_MOUSE;
    if (source == GDK_SOURCE_TOUCHSCREEN) {
        return FALSE;
    }

    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);

    const auto& pages = m_pane.pages();
    if (m_activePageIndex >= pages.size()) {
        return FALSE;
    }

    const auto& layout = pages[m_activePageIndex];
    const double xp = event->x - pageX;
    const double yp = event->y - layout.y;

    gdouble pressure = 1.0;
    if (!gdk_event_get_axis(reinterpret_cast<GdkEvent*>(event), GDK_AXIS_PRESSURE, &pressure) ||
        pressure <= 0.0) {
        pressure = 1.0;
    }

    m_activeStroke.pressures.push_back(pressure);
    m_activeStroke.points.push_back({xp, yp});
    m_lastPressure = pressure;

    gtk_widget_queue_draw(m_widget);
    return TRUE;
}

gboolean InkOverlay::onButtonRelease(GdkEventButton* event) {
    if (!m_isDrawing || event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    m_isDrawing = false;
    if (!m_activeStroke.points.empty()) {
        const std::size_t numPoints = m_activeStroke.points.size();
        if (numPoints > 1 && m_activeStroke.pressures.size() < numPoints - 1) {
            m_activeStroke.pressures.resize(numPoints - 1, 1.0);
        } else if (m_activeStroke.pressures.size() > numPoints - 1) {
            m_activeStroke.pressures.resize(numPoints > 0 ? numPoints - 1 : 0);
        }

        m_annotationStore.addStroke(m_activePageIndex, std::move(m_activeStroke));
    }

    m_activeStroke = FluidCore::Stroke{};
    gtk_widget_queue_draw(m_widget);
    return TRUE;
}

void InkOverlay::renderStroke(cairo_t* cr, const FluidCore::Stroke& stroke) const {
    if (stroke.points.empty()) {
        return;
    }

    const double r = ((stroke.color >> 16) & 0xFF) / 255.0;
    const double g = ((stroke.color >> 8) & 0xFF) / 255.0;
    const double b = (stroke.color & 0xFF) / 255.0;

    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    const bool isHighlighter = (stroke.tool == "highlighter");
    if (isHighlighter) {
        cairo_set_source_rgba(cr, r, g, b, 0.5);
    } else {
        cairo_set_source_rgb(cr, r, g, b);
    }

    if (stroke.points.size() == 1) {
        const double radius = std::max(0.5, stroke.width / 2.0);
        cairo_arc(cr, stroke.points[0].x, stroke.points[0].y, radius, 0.0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }

    const bool hasPressures =
        !stroke.pressures.empty() && stroke.pressures.size() == stroke.points.size() - 1;

    if (hasPressures) {
        for (std::size_t i = 0; i < stroke.pressures.size(); ++i) {
            const double segWidth = std::max(0.5, stroke.width * stroke.pressures[i]);
            cairo_set_line_width(cr, segWidth);
            cairo_move_to(cr, stroke.points[i].x, stroke.points[i].y);
            cairo_line_to(cr, stroke.points[i + 1].x, stroke.points[i + 1].y);
            cairo_stroke(cr);
        }
    } else {
        cairo_set_line_width(cr, std::max(0.5, stroke.width));
        cairo_move_to(cr, stroke.points[0].x, stroke.points[0].y);
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            cairo_line_to(cr, stroke.points[i].x, stroke.points[i].y);
        }
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

void InkOverlay::draw(cairo_t* cr) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);

    GdkRectangle clip;
    if (!gdk_cairo_get_clip_rectangle(cr, &clip)) {
        clip.x = 0;
        clip.y = 0;
        clip.width = allocation.width;
        clip.height = allocation.height;
    }

    const auto& pages = m_pane.pages();
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& layout = pages[i];
        if (layout.y + layout.height < clip.y || layout.y > clip.y + clip.height) {
            continue;
        }

        cairo_save(cr);
        cairo_translate(cr, pageX, layout.y);

        const std::vector<FluidCore::Stroke> pageStrokes = m_annotationStore.strokesForPage(i);
        for (const FluidCore::Stroke& stroke : pageStrokes) {
            renderStroke(cr, stroke);
        }

        if (m_isDrawing && m_activePageIndex == i) {
            renderStroke(cr, m_activeStroke);
        }

        cairo_restore(cr);
    }
}

} // namespace FluidCoreApp
