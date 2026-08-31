#pragma once

#include "services/StrokeStabilizer.h"
#include "services/TextSelectionService.h"
#include "storage/AnnotationStore.h"
#include "text/TextSelection.h"

#include <cairo.h>
#include <gtk/gtk.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FluidCoreApp {

class DocumentPane;

// GtkDrawingArea overlay on DocumentPane capturing pointer/stylus input,
// streaming coordinates through StrokeStabilizer (Centripetal Catmull-Rom),
// supporting text selection, drag-out excerpts via GTK Drag-and-Drop,
// rendering live inking with wet leading edge and text selection highlights,
// and dispatching AddStrokeCommand/RemoveStrokeCommand to DocumentPane's UndoStack.
class InkOverlay {
  public:
    InkOverlay(DocumentPane& pane, FluidCore::AnnotationStore& store);
    ~InkOverlay();

    InkOverlay(const InkOverlay&) = delete;
    InkOverlay& operator=(const InkOverlay&) = delete;

    GtkWidget* widget() const { return m_widget; }

    void setTool(const std::string& tool);
    const std::string& tool() const { return m_currentTool; }

    void setColor(std::uint32_t color) { m_currentColor = color; }
    std::uint32_t color() const { return m_currentColor; }

    void setStrokeWidth(double width) { m_currentWidth = width; }
    double strokeWidth() const { return m_currentWidth; }

    StrokeStabilizer& stabilizer() { return m_stabilizer; }

    void invalidateStroke(const FluidCore::Stroke& stroke);
    void invalidatePage(std::size_t pageIdx);

    // Text selection queries and operations
    bool hasSelection() const { return m_selectionState.hasSelection; }
    const FluidCore::MultiPageSelectionState& selectionState() const { return m_selectionState; }
    std::string selectedText() const { return m_selectionState.fullText; }
    void clearSelection();
    bool copySelection();
    void invalidateSelection(const FluidCore::MultiPageSelectionState& state);

    // Visual diagram crop selection state
    struct CropSelectionState {
        bool hasSelection = false;
        std::size_t pageIndex = 0;
        FluidCore::Rectangle rectPt{0.0, 0.0, 0.0, 0.0};
        FluidCore::Rectangle normRect{0.0, 0.0, 1.0, 1.0};
    };

    bool hasCropSelection() const { return m_cropSelectionState.hasSelection; }
    const CropSelectionState& cropSelectionState() const { return m_cropSelectionState; }
    void clearCropSelection();
    void invalidateCropSelection();

    TextSelectionService& textSelectionService() { return m_textSelectionService; }

    // Hit-testing and normalized document bounds calculation for drag-out excerpts
    bool isPointInsideSelection(std::size_t pageIndex, double xp, double yp) const;
    bool isPointInsideCropSelection(std::size_t pageIndex, double xp, double yp) const;
    FluidCore::Rectangle computeNormalizedSelectionBounds(std::size_t pageIndex, double pageWidth,
                                                          double pageHeight) const;

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    static gboolean buttonPressCallback(GtkWidget* widget, GdkEventButton* event,
                                        gpointer userData);
    static gboolean motionNotifyCallback(GtkWidget* widget, GdkEventMotion* event,
                                         gpointer userData);
    static gboolean buttonReleaseCallback(GtkWidget* widget, GdkEventButton* event,
                                          gpointer userData);
    static void dragDataGetCallback(GtkWidget* widget, GdkDragContext* context,
                                    GtkSelectionData* data, guint info, guint time,
                                    gpointer userData);
    static void dragEndCallback(GtkWidget* widget, GdkDragContext* context, gpointer userData);

    void draw(cairo_t* cr);
    gboolean onButtonPress(GdkEventButton* event);
    gboolean onMotionNotify(GdkEventMotion* event);
    gboolean onButtonRelease(GdkEventButton* event);
    void onDragDataGet(GdkDragContext* context, GtkSelectionData* data, guint info, guint time);
    void onDragEnd(GdkDragContext* context);

    void updateCursor();

    void renderStroke(cairo_t* cr, const FluidCore::Stroke& stroke) const;
    void renderActiveLiveStroke(cairo_t* cr) const;
    void renderBezierSegment(cairo_t* cr, const StrokeStabilizer::BezierSegment& seg,
                             double baseWidth) const;
    void renderTextSelection(cairo_t* cr, std::size_t pageIndex) const;
    void renderCropSelection(cairo_t* cr, std::size_t pageIndex) const;

    DocumentPane& m_pane;
    FluidCore::AnnotationStore& m_annotationStore;
    GtkWidget* m_widget = nullptr;

    bool m_isDrawing = false;
    std::size_t m_activePageIndex = 0;
    FluidCore::Stroke m_activeStroke;
    double m_lastPressure = 1.0;

    StrokeStabilizer m_stabilizer;
    std::vector<StrokeStabilizer::BezierSegment> m_activeBezierSegments;
    StrokeStabilizer::Point2D m_wetTip;
    bool m_hasWetSegment = false;

    // Text selection state and service
    TextSelectionService m_textSelectionService;
    FluidCore::MultiPageSelectionState m_selectionState;
    bool m_isSelectingText = false;
    std::size_t m_dragStartPageIndex = 0;
    FluidCore::SelectionPoint m_dragStartPoint;

    // Visual diagram crop selection state
    CropSelectionState m_cropSelectionState;
    bool m_isSelectingCrop = false;
    FluidCore::SelectionPoint m_cropDragStartPoint;

    // Drag-out excerpt interaction state
    bool m_isPotentialExcerptDrag = false;
    double m_pressScreenX = 0.0;
    double m_pressScreenY = 0.0;
    std::size_t m_dragSourcePageIndex = 0;

    std::string m_currentTool = "pen";
    std::uint32_t m_currentColor = 0x000000;
    double m_currentWidth = 2.0;
};

} // namespace FluidCoreApp
