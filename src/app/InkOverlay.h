#pragma once

#include "storage/AnnotationStore.h"

#include <cairo.h>
#include <gtk/gtk.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FluidCoreApp {

class DocumentPane;

// GtkDrawingArea overlay on DocumentPane capturing pointer/stylus input,
// building stroke geometry (points + pressure + timestamp), rendering live
// in Cairo during draw, and delegating stroke persistence to AnnotationStore.
class InkOverlay {
  public:
    InkOverlay(DocumentPane& pane, FluidCore::AnnotationStore& store);
    ~InkOverlay();

    InkOverlay(const InkOverlay&) = delete;
    InkOverlay& operator=(const InkOverlay&) = delete;

    GtkWidget* widget() const { return m_widget; }

    void setTool(const std::string& tool) { m_currentTool = tool; }
    const std::string& tool() const { return m_currentTool; }

    void setColor(std::uint32_t color) { m_currentColor = color; }
    std::uint32_t color() const { return m_currentColor; }

    void setStrokeWidth(double width) { m_currentWidth = width; }
    double strokeWidth() const { return m_currentWidth; }

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    static gboolean buttonPressCallback(GtkWidget* widget, GdkEventButton* event,
                                        gpointer userData);
    static gboolean motionNotifyCallback(GtkWidget* widget, GdkEventMotion* event,
                                         gpointer userData);
    static gboolean buttonReleaseCallback(GtkWidget* widget, GdkEventButton* event,
                                          gpointer userData);

    void draw(cairo_t* cr);
    gboolean onButtonPress(GdkEventButton* event);
    gboolean onMotionNotify(GdkEventMotion* event);
    gboolean onButtonRelease(GdkEventButton* event);

    void renderStroke(cairo_t* cr, const FluidCore::Stroke& stroke) const;

    DocumentPane& m_pane;
    FluidCore::AnnotationStore& m_annotationStore;
    GtkWidget* m_widget = nullptr;

    bool m_isDrawing = false;
    std::size_t m_activePageIndex = 0;
    FluidCore::Stroke m_activeStroke;
    double m_lastPressure = 1.0;

    std::string m_currentTool = "pen";
    std::uint32_t m_currentColor = 0x000000;
    double m_currentWidth = 2.0;
};

} // namespace FluidCoreApp
