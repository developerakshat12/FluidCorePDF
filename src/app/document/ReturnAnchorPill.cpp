#include "ReturnAnchorPill.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace FluidCoreApp {
namespace {

void drawRoundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (w <= 0.0 || h <= 0.0)
        return;
    r = std::min(r, std::min(w / 2.0, h / 2.0));
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

} // namespace

ReturnAnchorPill::ReturnAnchorPill() {
    m_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_area, static_cast<gint>(ReturnAnchorPillGeometry::kDefaultWidth),
                                static_cast<gint>(ReturnAnchorPillGeometry::kDefaultHeight));
    gtk_widget_set_no_show_all(m_area, TRUE);

    gtk_widget_add_events(m_area, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK |
                                      GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(m_area, "draw", G_CALLBACK(ReturnAnchorPill::drawCallback), this);
    g_signal_connect(m_area, "button-press-event",
                     G_CALLBACK(ReturnAnchorPill::buttonPressCallback), this);
    g_signal_connect(m_area, "motion-notify-event",
                     G_CALLBACK(ReturnAnchorPill::motionNotifyCallback), this);
    g_signal_connect(m_area, "leave-notify-event",
                     G_CALLBACK(ReturnAnchorPill::leaveNotifyCallback), this);
}

ReturnAnchorPill::~ReturnAnchorPill() = default;

void ReturnAnchorPill::show(const std::string& excerptId, const std::string& snippet,
                            const FluidCore::Point& originWorldCoord) {
    m_excerptId = excerptId;
    m_snippet = snippet;
    m_originWorldCoord = originWorldCoord;
    m_visible = true;

    if (m_area && GTK_IS_WIDGET(m_area)) {
        gtk_widget_show(m_area);
        gtk_widget_queue_draw(m_area);
    }
}

void ReturnAnchorPill::hide() {
    m_visible = false;
    m_hoverReturn = false;
    m_hoverClose = false;
    if (m_area) {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            gdk_window_set_cursor(win, nullptr);
        }
        gtk_widget_hide(m_area);
    }
}

void ReturnAnchorPill::drawCallback(GtkWidget* /*widget*/, cairo_t* cr, gpointer userData) {
    auto* self = static_cast<ReturnAnchorPill*>(userData);
    if (self && self->m_visible) {
        self->draw(cr);
    }
}

void ReturnAnchorPill::draw(cairo_t* cr) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double w = alloc.width > 0 ? alloc.width : ReturnAnchorPillGeometry::kDefaultWidth;
    const double h = alloc.height > 0 ? alloc.height : ReturnAnchorPillGeometry::kDefaultHeight;
    const double radius = h / 2.0;

    // 1. Soft elevated drop shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.28);
    drawRoundedRect(cr, 2.0, 3.0, w - 4.0, h - 4.0, radius);
    cairo_fill(cr);

    // 2. Main capsule container background (dark glassmorphism slate theme)
    drawRoundedRect(cr, 0.0, 0.0, w, h, radius);
    cairo_set_source_rgba(cr, 0.06, 0.09, 0.16, 0.94);
    cairo_fill_preserve(cr);

    // 3. Ambient cyan glowing border
    cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.65);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);

    cairo_save(cr);
    drawRoundedRect(cr, 0.0, 0.0, w, h, radius);
    cairo_clip(cr);

    // 4. Return action hover highlight
    const double closeW = ReturnAnchorPillGeometry::kCloseButtonWidth;
    if (m_hoverReturn) {
        cairo_rectangle(cr, 0.0, 0.0, w - closeW, h);
        cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.15);
        cairo_fill(cr);
    }

    // 5. Close button hover highlight
    if (m_hoverClose) {
        cairo_rectangle(cr, w - closeW, 0.0, closeW, h);
        cairo_set_source_rgba(cr, 0.95, 0.27, 0.27, 0.25);
        cairo_fill(cr);
    }

    // 6. Return arrow icon [ ↶ ] drawn as crisp vector geometry
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.38, 0.82, 0.98, 1.0); // Vibrant sky cyan
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    const double rx = 18.0;
    const double ry = h / 2.0;

    // Curved shaft: arcs up from lower-right over the top to the left
    cairo_new_sub_path(cr);
    cairo_arc_negative(cr, rx + 1.0, ry + 1.0, 4.8, M_PI * 0.35, -M_PI * 0.75);
    cairo_stroke(cr);

    // Arrowhead at the tip (pointing down-left)
    const double tipX = rx + 1.0 - 4.8 * 0.7071;
    const double tipY = ry + 1.0 - 4.8 * 0.7071;
    cairo_move_to(cr, tipX + 3.6, tipY - 0.5);
    cairo_line_to(cr, tipX, tipY);
    cairo_line_to(cr, tipX - 0.5, tipY + 3.6);
    cairo_stroke(cr);
    cairo_restore(cr);

    // 7. Label: "Return to Workspace Excerpt" or ID
    std::string labelText = "Return to Excerpt";
    if (!m_excerptId.empty()) {
        std::string shortId = m_excerptId;
        if (shortId.size() > 16) {
            shortId = shortId.substr(0, 14) + "..";
        }
        labelText = "Return to #" + shortId;
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 11.5);
    cairo_set_source_rgba(cr, 0.94, 0.96, 0.98, 1.0);
    cairo_move_to(cr, 32.0, h * 0.65);
    cairo_show_text(cr, labelText.c_str());

    // 8. Divider line between return action and dismiss button
    const double divX = w - closeW;
    cairo_move_to(cr, divX, 6.0);
    cairo_line_to(cr, divX, h - 6.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.18);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // 9. Close button [ ✕ ] drawn as crisp vector geometry
    const double cx = divX + closeW / 2.0;
    const double cy = h / 2.0;
    const double r = 3.8;
    if (m_hoverClose) {
        cairo_set_source_rgba(cr, 1.0, 0.45, 0.45, 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.75, 0.80, 0.88, 0.75);
    }
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx - r, cy - r);
    cairo_line_to(cr, cx + r, cy + r);
    cairo_move_to(cr, cx + r, cy - r);
    cairo_line_to(cr, cx - r, cy + r);
    cairo_stroke(cr);

    cairo_restore(cr);
}

gboolean ReturnAnchorPill::buttonPressCallback(GtkWidget* /*widget*/, GdkEventButton* event,
                                               gpointer userData) {
    auto* self = static_cast<ReturnAnchorPill*>(userData);
    if (self && self->m_visible && event->button == GDK_BUTTON_PRIMARY) {
        return self->onButtonPress(event);
    }
    return FALSE;
}

gboolean ReturnAnchorPill::onButtonPress(GdkEventButton* event) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double w = alloc.width > 0 ? alloc.width : ReturnAnchorPillGeometry::kDefaultWidth;
    const double h = alloc.height > 0 ? alloc.height : ReturnAnchorPillGeometry::kDefaultHeight;

    if (ReturnAnchorPillGeometry::isInsideCloseButton(event->x, event->y, w, h)) {
        if (m_onDismissClicked) {
            m_onDismissClicked();
        }
        hide();
        return TRUE;
    }

    if (ReturnAnchorPillGeometry::isInsideReturnAction(event->x, event->y, w, h)) {
        if (m_onReturnClicked) {
            m_onReturnClicked(m_originWorldCoord, m_excerptId);
        }
        return TRUE;
    }

    return FALSE;
}

gboolean ReturnAnchorPill::motionNotifyCallback(GtkWidget* /*widget*/, GdkEventMotion* event,
                                                gpointer userData) {
    auto* self = static_cast<ReturnAnchorPill*>(userData);
    if (self && self->m_visible) {
        return self->onMotionNotify(event);
    }
    return FALSE;
}

gboolean ReturnAnchorPill::onMotionNotify(GdkEventMotion* event) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(m_area, &alloc);
    const double w = alloc.width > 0 ? alloc.width : ReturnAnchorPillGeometry::kDefaultWidth;
    const double h = alloc.height > 0 ? alloc.height : ReturnAnchorPillGeometry::kDefaultHeight;

    const bool prevReturn = m_hoverReturn;
    const bool prevClose = m_hoverClose;

    m_hoverClose = ReturnAnchorPillGeometry::isInsideCloseButton(event->x, event->y, w, h);
    m_hoverReturn = ReturnAnchorPillGeometry::isInsideReturnAction(event->x, event->y, w, h);

    if (m_hoverReturn != prevReturn || m_hoverClose != prevClose) {
        if (m_area && GTK_IS_WIDGET(m_area)) {
            gtk_widget_queue_draw(m_area);
        }
    }

    if (m_area && GTK_IS_WIDGET(m_area)) {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            GdkDisplay* display = gdk_window_get_display(win);
            if (m_hoverReturn || m_hoverClose) {
                GdkCursor* pointerCursor = gdk_cursor_new_for_display(display, GDK_HAND2);
                gdk_window_set_cursor(win, pointerCursor);
                if (pointerCursor)
                    g_object_unref(pointerCursor);
            } else {
                gdk_window_set_cursor(win, nullptr);
            }
        }
    }

    return TRUE;
}

gboolean ReturnAnchorPill::leaveNotifyCallback(GtkWidget* /*widget*/, GdkEventCrossing* event,
                                               gpointer userData) {
    auto* self = static_cast<ReturnAnchorPill*>(userData);
    if (self) {
        return self->onLeaveNotify(event);
    }
    return FALSE;
}

gboolean ReturnAnchorPill::onLeaveNotify(GdkEventCrossing* /*event*/) {
    m_hoverReturn = false;
    m_hoverClose = false;
    if (m_area && GTK_IS_WIDGET(m_area)) {
        GdkWindow* win = gtk_widget_get_window(m_area);
        if (win) {
            gdk_window_set_cursor(win, nullptr);
        }
        gtk_widget_queue_draw(m_area);
    }
    return TRUE;
}

} // namespace FluidCoreApp
