#include "WorkspaceView.h"

#include <cmath>

namespace FluidCoreApp {
namespace {

constexpr double kGridStep = 32.0;

void drawBackgroundGrid(cairo_t* cr, double originX, double originY, double zoom, int width,
                        int height) {
    const double step = kGridStep * zoom;
    if (step < 8.0)
        return; // zoomed too far out for the grid to be useful

    cairo_set_source_rgb(cr, 0.906, 0.906, 0.894);
    cairo_set_line_width(cr, 1.0);

    double startX = std::fmod(-originX * zoom, step);
    if (startX < 0)
        startX += step;
    for (double x = startX - step; x < width; x += step) {
        cairo_move_to(cr, x, 0.0);
        cairo_line_to(cr, x, height);
    }

    double startY = std::fmod(-originY * zoom, step);
    if (startY < 0)
        startY += step;
    for (double y = startY - step; y < height; y += step) {
        cairo_move_to(cr, 0.0, y);
        cairo_line_to(cr, width, y);
    }

    cairo_stroke(cr);
}

} // namespace

WorkspaceView::WorkspaceView(FluidCore::FluidCoreAPI& api) : m_api(api) {
    m_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(m_area, TRUE);
    g_signal_connect(m_area, "draw", G_CALLBACK(WorkspaceView::drawCallback), this);
}

void WorkspaceView::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    auto* self = static_cast<WorkspaceView*>(userData);
    GtkAllocation rect;
    gtk_widget_get_allocation(self->m_area, &rect);
    self->draw(cr, rect.width, rect.height);
}

void WorkspaceView::draw(cairo_t* cr, int width, int height) {
    cairo_set_source_rgb(cr, 0.984, 0.984, 0.973);
    cairo_paint(cr);

    drawBackgroundGrid(cr, m_originX, m_originY, m_zoom, width, height);

    const FluidCore::Rectangle viewport{m_originX, m_originY, width / m_zoom, height / m_zoom};
    for (const FluidCore::WorkspaceNode* node : m_api.queryVisibleNodes(viewport)) {
        const FluidCore::Rectangle b = node->bounds();
        const double sx = (b.x - m_originX) * m_zoom;
        const double sy = (b.y - m_originY) * m_zoom;
        const double sw = b.w * m_zoom;
        const double sh = b.h * m_zoom;

        cairo_set_source_rgb(cr, 0.784, 0.863, 0.965);
        cairo_rectangle(cr, sx, sy, sw, sh);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.255, 0.412, 0.671);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, sx + 5.0, sy + 16.0);
        cairo_show_text(cr, node->id().c_str());
    }
}

} // namespace FluidCoreApp
