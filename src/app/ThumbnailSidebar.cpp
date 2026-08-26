#include "ThumbnailSidebar.h"

#include <algorithm>
#include <string>

namespace FluidCoreApp {

ThumbnailSidebar::ThumbnailSidebar(const std::vector<PageEntry>& pages)
    : ThumbnailSidebar(pages, ThumbnailLayout::LayoutConfig{}) {}

ThumbnailSidebar::ThumbnailSidebar(const std::vector<PageEntry>& pages,
                                   const ThumbnailLayout::LayoutConfig& config)
    : m_config(config), m_pages(pages) {
    m_scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);

    std::vector<ThumbnailLayout::PageDimension> dims;
    dims.reserve(m_pages.size());
    for (const auto& p : m_pages) {
        dims.push_back({p.width, p.height, p.docY});
    }
    m_layout = ThumbnailLayout::computeLayout(dims, m_config);

    m_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_area, static_cast<int>(std::round(m_layout.totalWidth)),
                                static_cast<int>(std::round(m_layout.totalHeight)));

    gtk_widget_add_events(m_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(m_area, "draw", G_CALLBACK(ThumbnailSidebar::drawCallback), this);
    g_signal_connect(m_area, "button-press-event",
                     G_CALLBACK(ThumbnailSidebar::buttonPressCallback), this);

    gtk_container_add(GTK_CONTAINER(m_scroller), m_area);
}

ThumbnailSidebar::~ThumbnailSidebar() = default;

void ThumbnailSidebar::setActivePage(std::size_t pageIndex) {
    if (pageIndex >= m_pages.size()) {
        return;
    }
    if (m_activePage != pageIndex) {
        m_activePage = pageIndex;
        if (m_area) {
            gtk_widget_queue_draw(m_area);
        }
        scrollToActiveThumbnail();
    }
}

void ThumbnailSidebar::scrollToActiveThumbnail() {
    if (m_activePage >= m_layout.boxes.size() || !m_scroller) {
        return;
    }

    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
    if (!vadj) {
        return;
    }

    const auto& box = m_layout.boxes[m_activePage];
    const double currentVal = gtk_adjustment_get_value(vadj);
    const double pageSize = gtk_adjustment_get_page_size(vadj);
    const double thumbTop = box.y;
    const double thumbBottom = box.y + box.height + m_config.labelHeight;

    if (thumbTop < currentVal) {
        gtk_adjustment_set_value(vadj, std::max(0.0, thumbTop - m_config.margin));
    } else if (thumbBottom > currentVal + pageSize && pageSize > 0.0) {
        const double upper = gtk_adjustment_get_upper(vadj);
        const double target = std::min(upper - pageSize, thumbBottom - pageSize + m_config.margin);
        gtk_adjustment_set_value(vadj, std::max(0.0, target));
    }
}

void ThumbnailSidebar::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    static_cast<ThumbnailSidebar*>(userData)->draw(cr);
}

void ThumbnailSidebar::draw(cairo_t* cr) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_area, &allocation);

    // Sidebar background fill
    cairo_set_source_rgb(cr, 0.941, 0.941, 0.933);
    cairo_paint(cr);

    GdkRectangle clip;
    if (!gdk_cairo_get_clip_rectangle(cr, &clip)) {
        clip.x = 0;
        clip.y = 0;
        clip.width = allocation.width;
        clip.height = allocation.height;
    }

    for (const auto& box : m_layout.boxes) {
        const double itemBottom = box.y + box.height + m_config.labelHeight;
        if (itemBottom < clip.y || box.y > clip.y + clip.height) {
            continue;
        }

        const bool isActive = (box.pageIndex == m_activePage);

        // Active page indicator highlight frame
        if (isActive) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 0.208, 0.518, 0.894);
            cairo_set_line_width(cr, 2.5);
            cairo_rectangle(cr, box.x - 3.0, box.y - 3.0, box.width + 6.0, box.height + 6.0);
            cairo_stroke(cr);
            cairo_restore(cr);
        }

        // Thumbnail surface
        cairo_surface_t* surface = m_cache.get(box.pageIndex);
        if (!surface && box.pageIndex < m_pages.size() && m_pages[box.pageIndex].page) {
            surface = m_cache.renderThumbnail(box.pageIndex, m_pages[box.pageIndex].page, box.width,
                                              box.height);
        }

        if (surface) {
            cairo_save(cr);
            cairo_set_source_surface(cr, surface, box.x, box.y);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            // Placeholder box while rendering
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_rectangle(cr, box.x, box.y, box.width, box.height);
            cairo_fill(cr);
            cairo_restore(cr);
        }

        // Thumbnail outline border
        cairo_save(cr);
        cairo_set_source_rgb(cr, isActive ? 0.208 : 0.72, isActive ? 0.518 : 0.72,
                             isActive ? 0.894 : 0.70);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, box.x, box.y, box.width, box.height);
        cairo_stroke(cr);
        cairo_restore(cr);

        // Page number badge
        cairo_save(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               isActive ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11.0);
        if (isActive) {
            cairo_set_source_rgb(cr, 0.12, 0.35, 0.75);
        } else {
            cairo_set_source_rgb(cr, 0.40, 0.40, 0.40);
        }

        const std::string label = std::to_string(box.pageIndex + 1);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, label.c_str(), &extents);

        const double textX = box.x + (box.width - extents.width) * 0.5 - extents.x_bearing;
        const double textY = box.y + box.height + m_config.labelHeight * 0.75;
        cairo_move_to(cr, textX, textY);
        cairo_show_text(cr, label.c_str());
        cairo_restore(cr);
    }
}

gboolean ThumbnailSidebar::buttonPressCallback(GtkWidget*, GdkEventButton* event,
                                               gpointer userData) {
    return static_cast<ThumbnailSidebar*>(userData)->onButtonPress(event);
}

gboolean ThumbnailSidebar::onButtonPress(GdkEventButton* event) {
    if (event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    auto hit = ThumbnailLayout::findPageAtY(m_layout.boxes, event->y, m_config);
    if (hit.has_value()) {
        const std::size_t pageIdx = *hit;
        setActivePage(pageIdx);
        if (m_pageSelectedCallback) {
            m_pageSelectedCallback(pageIdx);
        }
        return TRUE;
    }

    return FALSE;
}

} // namespace FluidCoreApp
