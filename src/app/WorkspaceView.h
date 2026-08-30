#pragma once

#include "ExcerptTileCache.h"
#include "FluidCoreAPI.h"
#include "StrokeStabilizer.h"
#include "storage/AnnotationStore.h"
#include "workspace/ExcerptCardNode.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtk/gtk.h>

namespace FluidCoreApp {

// Right-pane workspace canvas (specs/integration.md §1, TRD §3.4):
// An infinite 2D viewport (WorkspaceView) rendered via Cairo with 2D affine
// transformation matrix M_view, smooth focal pan/zoom gestures, zoom-adaptive
// infinite dot/grid background, floating overview minimap HUD, drag-and-drop
// excerpt destination, high-DPI visual diagram crop rendering, and O(log N) spatial index viewport
// culling.
class WorkspaceView {
  public:
    using NavigateToSourceCallback =
        std::function<void(const std::string& docId, std::size_t pageNo,
                           const FluidCore::Rectangle& normRect, const std::string& excerptId,
                           const std::string& snippet, const FluidCore::Point& cardWorldCenter)>;

    using ExcerptAddedCallback = std::function<void(const FluidCore::ExcerptCardNode&)>;

    explicit WorkspaceView(FluidCore::FluidCoreAPI& api);
    ~WorkspaceView();

    WorkspaceView(const WorkspaceView&) = delete;
    WorkspaceView& operator=(const WorkspaceView&) = delete;

    GtkWidget* widget() const { return m_area; }

    void setExcerptTileCache(ExcerptTileCache* cache);
    ExcerptTileCache* excerptTileCache() const { return m_excerptTileCache; }

    // Coordinate conversions between screen pixels and infinite world space
    FluidCore::Point screenToWorld(double screenX, double screenY) const;
    FluidCore::Point worldToScreen(double worldX, double worldY) const;

    // Viewport navigation controls
    void zoomAt(double factor, double focalScreenX, double focalScreenY);
    void setZoom(double zoom);
    void panBy(double dxScreen, double dyScreen);
    void centerOn(double worldX, double worldY);
    void glideToWorldCoord(double targetWorldX, double targetWorldY);
    void flashExcerptCard(const std::string& cardId);
    void resetView();

    double zoom() const { return m_zoom; }
    double originX() const { return m_originX; }
    double originY() const { return m_originY; }

    bool isMinimapVisible() const { return m_showMinimap; }
    void setMinimapVisible(bool visible);

    void setTool(const std::string& tool);
    const std::string& tool() const { return m_currentTool; }
    void setColor(uint32_t color) { m_currentColor = color; }
    void setStrokeWidth(double width) { m_currentWidth = width; }

    void setNavigateToSourceCallback(NavigateToSourceCallback cb) {
        m_onNavigateToSource = std::move(cb);
    }
    void setOnExcerptAddedCallback(ExcerptAddedCallback cb) { m_onExcerptAdded = std::move(cb); }

    void setSpacePressed(bool pressed);
    bool isSpacePressed() const { return m_isSpacePressed; }

    FluidCore::Rectangle getExcerptAnchorPillRect(const FluidCore::WorkspaceNode* node) const;

  private:
    struct TextLayoutCacheEntry {
        std::string text;
        double cardWidth = 0.0;
        double fontSize = 0.0;
        std::vector<std::string> lines;
    };

    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    static gboolean scrollCallback(GtkWidget* widget, GdkEventScroll* event, gpointer userData);
    static gboolean buttonPressCallback(GtkWidget* widget, GdkEventButton* event,
                                        gpointer userData);
    static gboolean buttonReleaseCallback(GtkWidget* widget, GdkEventButton* event,
                                          gpointer userData);
    static gboolean motionCallback(GtkWidget* widget, GdkEventMotion* event, gpointer userData);
    static gboolean keyPressCallback(GtkWidget* widget, GdkEventKey* event, gpointer userData);
    static gboolean keyReleaseCallback(GtkWidget* widget, GdkEventKey* event, gpointer userData);

    static gboolean zoomSettlingTimeoutCallback(gpointer userData);
    void onZoomSettled();

    // GTK3 Drag and Drop destination callbacks
    static void dragDataReceivedCallback(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
                                         GtkSelectionData* data, guint info, guint time,
                                         gpointer userData);
    static gboolean dragMotionCallback(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
                                       guint time, gpointer userData);
    static void dragLeaveCallback(GtkWidget* widget, GdkDragContext* context, guint time,
                                  gpointer userData);

    void draw(cairo_t* cr, int width, int height);
    void drawBackgroundGrid(cairo_t* cr, int width, int height);
    void drawMinimap(cairo_t* cr, int width, int height);
    void drawExcerptCard(cairo_t* cr, const FluidCore::WorkspaceNode* node, double sx, double sy,
                         double sw, double sh);
    void drawGenericNode(cairo_t* cr, const FluidCore::WorkspaceNode* node, double sx, double sy,
                         double sw, double sh);

    gboolean onScroll(GdkEventScroll* event);
    gboolean onButtonPress(GdkEventButton* event);
    gboolean onButtonRelease(GdkEventButton* event);
    gboolean onMotion(GdkEventMotion* event);
    gboolean onKeyPress(GdkEventKey* event);
    gboolean onKeyRelease(GdkEventKey* event);

    void onDragDataReceived(GdkDragContext* context, gint x, gint y, GtkSelectionData* data,
                            guint info, guint time);
    gboolean onDragMotion(GdkDragContext* context, gint x, gint y, guint time);
    void onDragLeave(GdkDragContext* context, guint time);

    FluidCore::Rectangle getMinimapRect(int viewWidth, int viewHeight) const;
    bool minimapHitTest(double screenX, double screenY, int viewWidth, int viewHeight) const;
    void handleMinimapInteraction(double screenX, double screenY, int viewWidth, int viewHeight);

    FluidCore::FluidCoreAPI& m_api;
    GtkWidget* m_area = nullptr;
    ExcerptTileCache* m_excerptTileCache = nullptr;

    // Viewport affine transform state (screen = (world - origin) * zoom)
    double m_originX = 0.0;
    double m_originY = 0.0;
    double m_zoom = 1.0;

    // Navigation and dragging interaction state
    bool m_isPanning = false;
    bool m_isSpacePressed = false;
    bool m_isMinimapDragging = false;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;

    // Drag-and-drop drop hover feedback
    bool m_isDropHovering = false;
    double m_dropHoverScreenX = 0.0;
    double m_dropHoverScreenY = 0.0;

    // Canvas inking state
    std::string m_currentTool = "select";
    uint32_t m_currentColor = 0x000000;
    double m_currentWidth = 1.5;

    StrokeStabilizer m_stabilizer;
    bool m_isDrawing = false;
    FluidCore::Stroke m_activeStroke;
    std::vector<StrokeStabilizer::BezierSegment> m_activeSegments;
    StrokeStabilizer::Point2D m_activeWetTip;
    bool m_hasWetSegment = false;

    // Minimap display settings
    bool m_showMinimap = true;
    double m_minimapWidth = 200.0;
    double m_minimapHeight = 140.0;
    double m_minimapMargin = 16.0;

    // Bi-directional anchor navigation & callbacks
    NavigateToSourceCallback m_onNavigateToSource;
    ExcerptAddedCallback m_onExcerptAdded;

    // Camera gliding animation state
    guint m_glideTimerId = 0;
    double m_glideStartX = 0.0;
    double m_glideStartY = 0.0;
    double m_glideTargetX = 0.0;
    double m_glideTargetY = 0.0;
    gint64 m_glideStartTimeUs = 0;

    // Card focus flash animation state
    std::string m_flashCardId;
    double m_flashAlpha = 0.0;
    guint m_flashTimerId = 0;
    gint64 m_flashStartTimeUs = 0;

    // Anchor hover state
    std::string m_hoveredAnchorCardId;

    // Zoom debounce & dynamic text layout cache
    guint m_zoomSettlingTimerId = 0;
    std::unordered_map<std::string, TextLayoutCacheEntry> m_textLayoutCache;
};

} // namespace FluidCoreApp
