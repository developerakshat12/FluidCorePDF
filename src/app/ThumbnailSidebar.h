#pragma once

#include "ThumbnailCache.h"
#include "ThumbnailLayout.h"

#include <cstddef>
#include <functional>
#include <vector>

#include <gtk/gtk.h>
#include <poppler.h>

namespace FluidCoreApp {

class DocumentPane;

// Thumbnail navigation sidebar widget (specs/integration.md §1, M1 Reader Core):
// A GtkScrolledWindow containing a custom GtkDrawingArea rendering cached thumbnail
// previews of PDF pages, page numbering badges, active-page highlight border,
// and handling click-to-scroll navigation.
class ThumbnailSidebar {
  public:
    using PageSelectedCallback = std::function<void(std::size_t)>;

    struct PageEntry {
        PopplerPage* page = nullptr;
        double width = 0.0;
        double height = 0.0;
        double docY = 0.0;
    };

    explicit ThumbnailSidebar(const std::vector<PageEntry>& pages);
    ThumbnailSidebar(const std::vector<PageEntry>& pages,
                     const ThumbnailLayout::LayoutConfig& config);
    ~ThumbnailSidebar();

    ThumbnailSidebar(const ThumbnailSidebar&) = delete;
    ThumbnailSidebar& operator=(const ThumbnailSidebar&) = delete;

    GtkWidget* widget() const { return m_scroller; }

    void setPageSelectedCallback(PageSelectedCallback callback) {
        m_pageSelectedCallback = std::move(callback);
    }

    std::size_t activePage() const { return m_activePage; }
    void setActivePage(std::size_t pageIndex);
    void scrollToActiveThumbnail();

  private:
    static void drawCallback(GtkWidget* area, cairo_t* cr, gpointer userData);
    void draw(cairo_t* cr);

    static gboolean buttonPressCallback(GtkWidget* area, GdkEventButton* event, gpointer userData);
    gboolean onButtonPress(GdkEventButton* event);

    GtkWidget* m_scroller = nullptr;
    GtkWidget* m_area = nullptr;

    ThumbnailLayout::LayoutConfig m_config;
    ThumbnailLayout::LayoutResult m_layout;
    ThumbnailCache m_cache;
    std::vector<PageEntry> m_pages;

    std::size_t m_activePage = 0;
    PageSelectedCallback m_pageSelectedCallback;
};

} // namespace FluidCoreApp
