#pragma once

#include "storage/AnnotationStore.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>
#include <poppler.h>

namespace FluidCoreApp {

class InkOverlay;

// Left-pane document viewport (specs/integration.md §1, scoped-down shell):
// a GtkScrolledWindow hosting a GtkOverlay containing the Poppler-rendered
// PDF DrawingArea and the interactive InkOverlay for live annotation.
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

    GtkWidget* widget() const { return m_scroller; }

    bool save() { return saveAnnotations(); }
    bool saveAnnotations();

    const std::string& pdfPath() const { return m_pdfPath; }
    const std::vector<PageLayout>& pages() const { return m_pages; }
    double layoutWidth() const { return m_layoutWidth; }
    double layoutHeight() const { return m_layoutHeight; }

    FluidCore::AnnotationStore& annotationStore() { return m_annotationStore; }
    const FluidCore::AnnotationStore& annotationStore() const { return m_annotationStore; }
    InkOverlay* inkOverlay() const { return m_inkOverlay.get(); }

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    void draw(cairo_t* cr);

    std::string m_pdfPath;
    GtkWidget* m_scroller = nullptr;
    GtkWidget* m_overlay = nullptr;
    GtkWidget* m_area = nullptr;
    PopplerDocument* m_document = nullptr;
    std::vector<PageLayout> m_pages;
    double m_layoutWidth = 0.0;
    double m_layoutHeight = 0.0;

    FluidCore::AnnotationStore m_annotationStore;
    std::unique_ptr<InkOverlay> m_inkOverlay;
};

} // namespace FluidCoreApp
