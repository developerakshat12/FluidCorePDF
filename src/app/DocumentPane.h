#pragma once

#include <string>
#include <vector>

#include <gtk/gtk.h>
#include <poppler.h>

namespace FluidCoreApp {

// Left-pane document viewport (specs/integration.md §1, scoped-down shell):
// a GtkScrolledWindow hosting a GtkDrawingArea that renders PDF pages through
// poppler-glib during the Cairo draw pass only. All GUI/library calls stay in
// src/app per ADR-0001; squeeze mapping slots between Poppler geometry and
// screen coordinates in M2.
class DocumentPane {
  public:
    // Empty path shows an empty-state label instead of a document.
    explicit DocumentPane(const std::string& pdfPath);

    ~DocumentPane();

    DocumentPane(const DocumentPane&) = delete;
    DocumentPane& operator=(const DocumentPane&) = delete;

    GtkWidget* widget() const { return m_scroller; }

  private:
    struct PageLayout {
        PopplerPage* page = nullptr;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    void draw(cairo_t* cr);

    GtkWidget* m_scroller = nullptr;
    GtkWidget* m_area = nullptr;
    PopplerDocument* m_document = nullptr;
    std::vector<PageLayout> m_pages;
    double m_layoutWidth = 0.0;
    double m_layoutHeight = 0.0;
};

} // namespace FluidCoreApp
