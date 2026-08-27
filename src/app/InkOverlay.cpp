#include "InkOverlay.h"
#include "DamageRect.h"
#include "DocumentPane.h"

#include <algorithm>
#include <cmath>

namespace FluidCoreApp {
namespace {

constexpr double kPageMargin = 12.0;

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
        return FALSE;
    }

    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);

    const auto& pages = m_pane.pages();
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& layout = pages[i];
        if (event->y >= layout.y && event->y <= layout.y + layout.height && event->x >= pageX &&
            event->x <= pageX + layout.width) {
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

            m_stabilizer.beginStroke({xp, yp}, pressure, static_cast<std::uint64_t>(event->time),
                                     StabilizerMode::Smooth);
            m_activeBezierSegments.clear();
            m_wetTip = {xp, yp};
            m_hasWetSegment = false;

            // Invalidate initial touch region
            const auto damage = DamageRect::computePointDamage({event->x, event->y},
                                                               m_activeStroke.width * pressure);
            gtk_widget_queue_draw_area(m_widget, damage.x, damage.y, damage.width, damage.height);
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

    const double prevTipScreenX = pageX + m_wetTip.x;
    const double prevTipScreenY = layout.y + m_wetTip.y;

    auto pushResult =
        m_stabilizer.pushPoint({xp, yp}, pressure, static_cast<std::uint64_t>(event->time));

    // Invalidate newly committed Bezier curve areas using convex-hull bounds
    for (const auto& seg : pushResult.newlyCommitted) {
        m_activeBezierSegments.push_back(seg);
        const DamageRect::Point2D b0 = {pageX + seg.p0.x, layout.y + seg.p0.y};
        const DamageRect::Point2D b1 = {pageX + seg.p1.x, layout.y + seg.p1.y};
        const DamageRect::Point2D b2 = {pageX + seg.p2.x, layout.y + seg.p2.y};
        const DamageRect::Point2D b3 = {pageX + seg.p3.x, layout.y + seg.p3.y};
        const double maxP = std::max(seg.pressure0, seg.pressure1);
        const auto damage =
            DamageRect::computeBezierDamage(b0, b1, b2, b3, m_activeStroke.width * maxP);
        gtk_widget_queue_draw_area(m_widget, damage.x, damage.y, damage.width, damage.height);
    }

    m_wetTip = pushResult.wetTip;
    m_hasWetSegment = pushResult.hasWetSegment;

    // Invalidate the live wet leading edge segment
    const double currTipScreenX = pageX + m_wetTip.x;
    const double currTipScreenY = layout.y + m_wetTip.y;
    const double maxP = std::max(m_lastPressure, pressure);
    const auto leadDamage = DamageRect::computeSegmentDamage({prevTipScreenX, prevTipScreenY},
                                                             {currTipScreenX, currTipScreenY},
                                                             m_activeStroke.width * maxP);
    gtk_widget_queue_draw_area(m_widget, leadDamage.x, leadDamage.y, leadDamage.width,
                               leadDamage.height);

    m_lastPressure = pressure;
    return TRUE;
}

gboolean InkOverlay::onButtonRelease(GdkEventButton* event) {
    if (!m_isDrawing || event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    m_isDrawing = false;
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_widget, &allocation);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width - m_pane.layoutWidth()) / 2.0);

    const auto& pages = m_pane.pages();
    if (m_activePageIndex < pages.size()) {
        const auto& layout = pages[m_activePageIndex];
        auto tailSegs = m_stabilizer.endStroke();
        for (const auto& seg : tailSegs) {
            m_activeBezierSegments.push_back(seg);
            const DamageRect::Point2D b0 = {pageX + seg.p0.x, layout.y + seg.p0.y};
            const DamageRect::Point2D b1 = {pageX + seg.p1.x, layout.y + seg.p1.y};
            const DamageRect::Point2D b2 = {pageX + seg.p2.x, layout.y + seg.p2.y};
            const DamageRect::Point2D b3 = {pageX + seg.p3.x, layout.y + seg.p3.y};
            const double maxP = std::max(seg.pressure0, seg.pressure1);
            const auto damage =
                DamageRect::computeBezierDamage(b0, b1, b2, b3, m_activeStroke.width * maxP);
            gtk_widget_queue_draw_area(m_widget, damage.x, damage.y, damage.width, damage.height);
        }

        const auto& samples = m_stabilizer.rawSamples();
        if (!samples.empty()) {
            m_activeStroke.points.clear();
            m_activeStroke.pressures.clear();
            m_activeStroke.points.reserve(samples.size());
            if (samples.size() > 1) {
                m_activeStroke.pressures.reserve(samples.size() - 1);
            }

            for (std::size_t i = 0; i < samples.size(); ++i) {
                m_activeStroke.points.push_back({samples[i].point.x, samples[i].point.y});
                if (i > 0) {
                    m_activeStroke.pressures.push_back(samples[i].pressure);
                }
            }

            m_annotationStore.addStroke(m_activePageIndex, std::move(m_activeStroke));
        }
    }

    m_activeStroke = FluidCore::Stroke{};
    m_activeBezierSegments.clear();
    m_hasWetSegment = false;
    return TRUE;
}

void InkOverlay::renderBezierSegment(cairo_t* cr, const StrokeStabilizer::BezierSegment& seg,
                                     double baseWidth) const {
    // Subdivide Bezier segment at K=3 sub-spans to interpolate variable pressure smoothly
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

    // Render live wet leading edge from last committed curve endpoint to current stylus tip
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

    // Group isolation for translucent ink: prevents overlapping sub-span joints from
    // double-darkening
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
        // Fit Centripetal Catmull-Rom Bezier curves across stored stroke points
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

} // namespace FluidCoreApp
