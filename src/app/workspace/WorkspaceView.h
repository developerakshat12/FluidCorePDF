#pragma once

#include "FluidCoreAPI.h"
#include "undo/UndoStack.h"
#include "workspace/WorkspaceState.h"

#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace FluidCoreApp {

class ExcerptTileCache;

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
    GtkWidget* drawingArea() const { return m_area; }

    // Undo / Redo manager
    bool undo();
    bool redo();
    bool canUndo() const { return m_undoStack.canUndo(); }
    bool canRedo() const { return m_undoStack.canRedo(); }
    FluidCore::UndoStack& undoStack() { return m_undoStack; }
    const FluidCore::UndoStack& undoStack() const { return m_undoStack; }

    using ActivatedCallback = std::function<void()>;
    void setOnActivatedCallback(ActivatedCallback cb) { m_onActivated = std::move(cb); }
    void notifyActivated() {
        if (m_onActivated) {
            m_onActivated();
        }
    }

    // Transient interaction cancellation (Esc key / gesture discard)
    void cancelCurrentInteraction();
    void notifyModelReloaded();

    // Instant Inline In-Place Stack Renaming
    void startInlineStackRename(const std::string& stackId);
    void cancelInlineStackRename();
    void commitInlineStackRename();
    bool isInlineRenaming() const { return m_activeRenamePopover != nullptr; }

    void setExcerptTileCache(ExcerptTileCache* cache);
    ExcerptTileCache* excerptTileCache() const { return m_excerptTileCache; }

    // Coordinate conversions
    FluidCore::Point screenToWorld(double screenX, double screenY) const {
        return m_state.viewport.screenToWorld(screenX, screenY);
    }
    FluidCore::Point worldToScreen(double worldX, double worldY) const {
        return m_state.viewport.worldToScreen(worldX, worldY);
    }

    // Viewport navigation
    void zoomAt(double factor, double focalScreenX, double focalScreenY);
    void setZoom(double zoom);
    void panBy(double dxScreen, double dyScreen);
    void centerOn(double worldX, double worldY);
    void glideToWorldCoord(double targetWorldX, double targetWorldY);
    void flashExcerptCard(const std::string& cardId);
    void resetView();

    double zoom() const { return m_state.viewport.zoom; }
    double originX() const { return m_state.viewport.originX; }
    double originY() const { return m_state.viewport.originY; }

    bool isMinimapVisible() const { return m_state.showMinimap; }
    void setMinimapVisible(bool visible);

    // Workspace Search & Navigation API (TASK-4.3)
    void setSearchResults(std::vector<FluidCore::WorkspaceMatch> matches, const std::string& query,
                          int activeIndex = 0);
    void clearSearch();
    void navigateSearch(int direction);
    int activeSearchMatchIndex() const { return m_state.search.activeMatchIndex; }
    std::size_t searchMatchCount() const { return m_state.search.matches.size(); }
    const std::vector<FluidCore::WorkspaceMatch>& searchMatches() const {
        return m_state.search.matches;
    }

    void setTool(const std::string& tool);
    const std::string& tool() const { return m_state.inking.currentTool; }
    void setColor(uint32_t color) { m_state.inking.currentColor = color; }
    void setStrokeWidth(double width) { m_state.inking.currentWidth = width; }

    void setNavigateToSourceCallback(NavigateToSourceCallback cb) {
        m_onNavigateToSource = std::move(cb);
    }
    void setOnExcerptAddedCallback(ExcerptAddedCallback cb) { m_onExcerptAdded = std::move(cb); }

    void setSpacePressed(bool pressed);
    bool isSpacePressed() const { return m_state.isSpacePressed; }

    const WorkspaceState& state() const { return m_state; }
    WorkspaceState& state() { return m_state; }

  private:
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

    static void dragDataReceivedCallback(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
                                         GtkSelectionData* data, guint info, guint time,
                                         gpointer userData);
    static gboolean dragMotionCallback(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
                                       guint time, gpointer userData);
    static void dragLeaveCallback(GtkWidget* widget, GdkDragContext* context, guint time,
                                  gpointer userData);

    void draw(cairo_t* cr, int width, int height);
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

    FluidCore::FluidCoreAPI& m_api;
    GtkWidget* m_area = nullptr;
    GtkWidget* m_activeRenamePopover = nullptr;
    GtkWidget* m_activeRenameEntry = nullptr;
    std::string m_activeRenameStackId;

    ExcerptTileCache* m_excerptTileCache = nullptr;
    WorkspaceState m_state;
    FluidCore::UndoStack m_undoStack;

    NavigateToSourceCallback m_onNavigateToSource;
    ExcerptAddedCallback m_onExcerptAdded;
    ActivatedCallback m_onActivated;
};

} // namespace FluidCoreApp
