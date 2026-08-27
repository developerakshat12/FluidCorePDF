#pragma once

#include "PageTileCache.h"
#include "storage/AnnotationStore.h"
#include "undo/UndoStack.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>
#include <poppler.h>

namespace FluidCoreApp {

class InkOverlay;
class ThumbnailSidebar;

// Left-pane document viewport (specs/integration.md §1, M1 Reader Core):
// A horizontal GtkPaned hosting the ThumbnailSidebar on the left (with draggable divider)
// and a GtkScrolledWindow on the right containing the continuous Poppler PDF DrawingArea
// and the interactive InkOverlay for live annotation.
// Owns UndoStack for transactional stroke addition, erasure, and page clear actions.
// Uses PageTileCache with LRU byte budgeting and visible-page pinning for instant blits.
// On open <file>.pdf, automatically loads companion <file>.xopp if present;
// on close or explicit save (Ctrl+S), writes companion .xopp via AnnotationStore.
class DocumentPane {
  public:
    struct PageLayout {
        PopplerPage* page = nullptr;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    // Empty path shows an empty-state label instead of a document.
    explicit DocumentPane(const std::string& pdfPath);

    ~DocumentPane();

    DocumentPane(const DocumentPane&) = delete;
    DocumentPane& operator=(const DocumentPane&) = delete;

    GtkWidget* widget() const { return m_paned ? m_paned : m_scroller; }

    bool save() { return saveAnnotations(); }
    bool saveAnnotations();

    bool undo();
    bool redo();
    bool canUndo() const { return m_undoStack.canUndo(); }
    bool canRedo() const { return m_undoStack.canRedo(); }

    void scrollToPage(std::size_t pageIndex);
    void clearCache() { m_pageTileCache.clear(); }

    const std::string& pdfPath() const { return m_pdfPath; }
    const std::vector<PageLayout>& pages() const { return m_pages; }
    double layoutWidth() const { return m_layoutWidth; }
    double layoutHeight() const { return m_layoutHeight; }

    FluidCore::AnnotationStore& annotationStore() { return m_annotationStore; }
    const FluidCore::AnnotationStore& annotationStore() const { return m_annotationStore; }
    FluidCore::UndoStack& undoStack() { return m_undoStack; }
    const FluidCore::UndoStack& undoStack() const { return m_undoStack; }

    InkOverlay* inkOverlay() const { return m_inkOverlay.get(); }
    ThumbnailSidebar* thumbnailSidebar() const { return m_thumbnailSidebar.get(); }
    PageTileCache& pageTileCache() { return m_pageTileCache; }

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    void draw(cairo_t* cr);

    static void onScrollValueChanged(GtkAdjustment* adj, gpointer userData);

    std::string m_pdfPath;
    GtkWidget* m_paned = nullptr;
    GtkWidget* m_scroller = nullptr;
    GtkWidget* m_overlay = nullptr;
    GtkWidget* m_area = nullptr;
    PopplerDocument* m_document = nullptr;
    std::vector<PageLayout> m_pages;
    double m_layoutWidth = 0.0;
    double m_layoutHeight = 0.0;

    PageTileCache m_pageTileCache;
    FluidCore::AnnotationStore m_annotationStore;
    FluidCore::UndoStack m_undoStack;
    std::unique_ptr<InkOverlay> m_inkOverlay;
    std::unique_ptr<ThumbnailSidebar> m_thumbnailSidebar;
};

} // namespace FluidCoreApp
