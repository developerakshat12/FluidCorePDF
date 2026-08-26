#pragma once

#include "FluidCoreAPI.h"

#include <gtk/gtk.h>

namespace FluidCoreApp {

// Right-pane workspace canvas (specs/integration.md §1): a GtkDrawingArea whose
// Cairo pass renders whatever queryVisibleNodes returns for the current
// viewport, plus a background grid. All rendering stays in this layer per
// ADR-0001 — the engine hands over plain geometry values only.
//
// The view transform (M_view, TRD §3.4) maps world -> screen as
//   screen = (world - origin) * zoom
// and sits at identity until pan/zoom input lands in M1.
class WorkspaceView {
  public:
    explicit WorkspaceView(FluidCore::FluidCoreAPI& api);

    GtkWidget* widget() const { return m_area; }

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    void draw(cairo_t* cr, int width, int height);

    FluidCore::FluidCoreAPI& m_api;
    GtkWidget* m_area = nullptr;

    double m_originX = 0.0;
    double m_originY = 0.0;
    double m_zoom = 1.0;
};

} // namespace FluidCoreApp
