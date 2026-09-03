#pragma once

#include "document/ReturnAnchorPill.h"
#include "document/SearchBarWidget.h"
#include "search/AnchorSqueezePlanner.h"
#include "services/DocumentSearchService.h"
#include "services/PageTileCache.h"
#include "squeeze/SqueezeEngine.h"
#include "storage/AnnotationStore.h"
#include "undo/UndoStack.h"
#include "workspace/ExcerptCardNode.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>
#include <poppler.h>

namespace FluidCoreApp {

class InkOverlay;
class WorkspaceView;

struct PulseHighlightState {
    bool active = false;
    std::size_t pageNo = 0;
    FluidCore::Rectangle normRect{0.0, 0.0, 0.0, 0.0};
    double alpha = 0.0;
    gint64 startTimeUs = 0;
};

// Left-pane document viewport (specs/integration.md §1, M1 Reader Core & M2 Squeeze Engine):
// A continuous Poppler PDF DrawingArea with LRU tile caching, interactive InkOverlay,
// dynamic SqueezeEngine piecewise rendering, and search/highlight-driven accordion squeezing.
class DocumentPane {
  public:
    using PageLayout = SearchPageLayout;
    using ReturnToWorkspaceCallback =
        std::function<void(const FluidCore::Point& originWorldCoord, const std::string& excerptId)>;

    // Empty path shows an empty-state label instead of a document.
    explicit DocumentPane(const std::string& pdfPath);

    ~DocumentPane();

    DocumentPane(const DocumentPane&) = delete;
    DocumentPane& operator=(const DocumentPane&) = delete;

    GtkWidget* widget() const { return m_viewOverlay; }

    bool loadDocument(const std::string& pdfPath, const std::string& docId = "");
    void closeDocument();
    void repointCompanionPath(const std::string& newPdfPath);

    bool save() { return saveAnnotations(); }
    bool saveAnnotations();

    bool undo();
    bool redo();
    bool canUndo() const { return m_undoStack.canUndo(); }
    bool canRedo() const { return m_undoStack.canRedo(); }

    void setTool(const std::string& tool);
    const std::string& tool() const;

    bool hasTextSelection() const;
    void clearTextSelection();
    void clearCropSelection();
    bool copySelection();

    PopplerDocument* document() const { return m_document; }
    GtkWidget* scroller() const { return m_scroller; }
    void clearCache() { m_pageTileCache.clear(); }

    const std::string& pdfPath() const { return m_pdfPath; }
    const std::vector<PageLayout>& pages() const { return m_pages; }
    double layoutWidth() const { return m_layoutWidth; }
    double layoutHeight() const { return m_layoutHeight; }

    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void resetZoom();

    FluidCore::AnnotationStore& annotationStore() { return m_annotationStore; }
    const FluidCore::AnnotationStore& annotationStore() const { return m_annotationStore; }
    FluidCore::UndoStack& undoStack() { return m_undoStack; }
    const FluidCore::UndoStack& undoStack() const { return m_undoStack; }

    InkOverlay* inkOverlay() const { return m_inkOverlay.get(); }
    PageTileCache& pageTileCache() { return m_pageTileCache; }

    // Squeeze Engine facades for view & overlay components
    double docYToScreen(double docY) const;
    double screenYToDoc(double screenY) const;
    std::vector<FluidCore::SqueezeSegment> squeezeSegments() const;
    double totalSqueezedHeight() const;
    bool isSqueezed() const;

    FluidCore::SqueezeEngine& squeezeEngine() { return m_squeezeEngine; }
    const FluidCore::SqueezeEngine& squeezeEngine() const { return m_squeezeEngine; }
    const std::string& docId() const { return m_docId; }

    // Transactional Squeeze controls
    void setSqueezeFold(double yStart, double yEnd, double alpha);
    void resetSqueeze();
    void updateLayoutDimensions();

    // HighlightView & Excerpt anchor integration
    void setHighlightView(bool enable);
    bool isHighlightViewActive() const { return m_highlightViewActive; }
    void toggleHighlightView();
    void setExcerptAnchors(std::vector<FluidCore::AnchorSpan> anchors);
    void addExcerptAnchor(const FluidCore::ExcerptCardNode& card);
    void applyHighlightSqueeze();

    // Search-Driven Squeeze & Workspace Find Subsystem (TASK-4.3)
    void setWorkspaceContext(WorkspaceView* ws, FluidCore::FluidCoreAPI* api) {
        m_workspaceView = ws;
        m_coreApi = api;
    }
    void openSearch(bool enableSqueeze = true, SearchScope scope = SearchScope::Document);
    void closeSearch();
    void navigateSearch(int direction);
    void scrollToSearchHit(std::size_t hitIndex);
    bool isSearchActive() const;
    const std::vector<SearchHit>& searchHits() const { return m_searchHits; }
    std::size_t activeSearchHitIndex() const { return m_activeSearchHitIndex; }

    // Bi-directional Excerpt Source Navigation & Floating Return Pill
    void navigateToExcerptSource(std::size_t pageNo, const FluidCore::Rectangle& normRect,
                                 const std::string& excerptId, const std::string& snippet,
                                 const FluidCore::Point& originWorldCoord);

    void setOnReturnToWorkspaceCallback(ReturnToWorkspaceCallback cb) {
        m_onReturnToWorkspace = std::move(cb);
    }

    using ActivatedCallback = std::function<void()>;
    void setOnActivatedCallback(ActivatedCallback cb) { m_onActivated = std::move(cb); }
    void notifyActivated() {
        if (m_onActivated) {
            m_onActivated();
        }
    }

    ReturnAnchorPill* returnAnchorPill() const { return m_returnAnchorPill.get(); }
    const PulseHighlightState& pulseHighlight() const { return m_pulseHighlight; }

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    static gboolean scrollCallback(GtkWidget* widget, GdkEventScroll* event, gpointer userData);

    void draw(cairo_t* cr);
    gboolean onScroll(GdkEventScroll* event);
    void commitScrollSqueeze();
    void commitZoom();

    // Unified Anchor Squeeze Pipeline
    std::vector<FluidCore::AnchorSpan> collectActiveAnchors(double cursorDocY = -1.0) const;
    void applyContinuousSqueezeDelta(double delta, double cursorScreenY);
    void stabilizeViewportAroundCursor(double anchorDocY, double cursorScreenY);

    void onSearchQueryChanged(const std::string& query, bool enableSqueeze);
    void onSearchSqueezeToggled(bool enableSqueeze);
    void applySearchSqueeze();

    std::string m_pdfPath;
    std::string m_docId = "doc-primary";

    GtkWidget* m_viewOverlay = nullptr;
    GtkWidget* m_scroller = nullptr;
    GtkWidget* m_overlay = nullptr;
    GtkWidget* m_area = nullptr;
    GtkGesture* m_pinchGesture = nullptr;
    PopplerDocument* m_document = nullptr;
    std::vector<PageLayout> m_pages;
    double m_layoutWidth = 0.0;
    double m_layoutHeight = 0.0;

    PageTileCache m_pageTileCache;
    FluidCore::AnnotationStore m_annotationStore;
    FluidCore::UndoStack m_undoStack;
    FluidCore::SqueezeEngine m_squeezeEngine;
    std::unique_ptr<InkOverlay> m_inkOverlay;

    // Interactive downward fold squeeze state
    double m_activeFoldStartDocY = 0.0;
    double m_activeFoldSpan = 0.0;
    std::string m_activeFoldRegionId;
    bool m_hasActiveFoldGesture = false;
    guint m_wheelDebounceTimerId = 0;
    bool m_isAdjustingScrollPosition = false;

    // HighlightView & Excerpt anchor state
    bool m_highlightViewActive = false;
    double m_highlightSqueezeAlpha = 1.0;
    std::vector<FluidCore::AnchorSpan> m_excerptAnchors;

    double m_zoom = 1.0;
    bool m_isZooming = false;
    guint m_zoomDebounceTimerId = 0;

    // Search subsystem state
    std::unique_ptr<SearchBarWidget> m_searchBar;
    DocumentSearchService m_searchService;
    std::vector<SearchHit> m_searchHits;
    std::size_t m_activeSearchHitIndex = 0;
    double m_searchSqueezeAlpha = 0.04;

    // Bi-directional return pill & luminous pulse highlight state
    std::unique_ptr<ReturnAnchorPill> m_returnAnchorPill;
    ReturnToWorkspaceCallback m_onReturnToWorkspace;
    ActivatedCallback m_onActivated;
    PulseHighlightState m_pulseHighlight;
    guint m_pulseTimerId = 0;
    double m_savedReadingScrollY = 0.0;
    bool m_hasSavedReadingState = false;

    WorkspaceView* m_workspaceView = nullptr;
    FluidCore::FluidCoreAPI* m_coreApi = nullptr;
};

} // namespace FluidCoreApp
