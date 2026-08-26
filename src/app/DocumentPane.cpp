#include "DocumentPane.h"

#include <algorithm>

#include <cairo.h>

namespace FluidCoreApp {
namespace {

constexpr double kPageMargin = 12.0;
constexpr double kPageGap = 12.0;

GtkWidget* makeStatusLabel(const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);
    return label;
}

} // namespace

DocumentPane::DocumentPane(const std::string& pdfPath) {
    m_scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroller), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    if (pdfPath.empty()) {
        gtk_container_add(GTK_CONTAINER(m_scroller),
                          makeStatusLabel("No document loaded — pass a PDF path as the first "
                                          "argument."));
        return;
    }

    GError* error = nullptr;
    // poppler-glib expects a URI here across all supported versions.
    gchar* uri = g_filename_to_uri(pdfPath.c_str(), nullptr, nullptr);
    m_document = poppler_document_new_from_file(uri, nullptr, &error);
    g_free(uri);
    if (!m_document) {
        gchar* message = g_strdup_printf("Could not open PDF:\n%s\n\n(%s)", pdfPath.c_str(),
                                         error ? error->message : "unknown error");
        if (error)
            g_error_free(error);
        gtk_container_add(GTK_CONTAINER(m_scroller), makeStatusLabel(message));
        g_free(message);
        return;
    }

    const int pageCount = poppler_document_get_n_pages(m_document);
    double y = kPageMargin;
    for (int i = 0; i < pageCount; ++i) {
        PopplerPage* page = poppler_document_get_page(m_document, i);
        if (!page)
            continue;
        PageLayout layout;
        layout.page = page;
        poppler_page_get_size(page, &layout.width, &layout.height);
        layout.y = y;
        y += layout.height + kPageGap;
        m_layoutWidth = std::max(m_layoutWidth, layout.width);
        m_pages.push_back(layout);
    }
    m_layoutWidth += 2.0 * kPageMargin;
    m_layoutHeight = y;

    m_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_area, static_cast<int>(m_layoutWidth),
                                static_cast<int>(m_layoutHeight));
    g_signal_connect(m_area, "draw", G_CALLBACK(DocumentPane::drawCallback), this);

    gtk_container_add(GTK_CONTAINER(m_scroller), m_area);
}

DocumentPane::~DocumentPane() {
    for (PageLayout& layout : m_pages) {
        if (layout.page)
            g_object_unref(layout.page);
    }
    if (m_document)
        g_object_unref(m_document);
}

void DocumentPane::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    static_cast<DocumentPane*>(userData)->draw(cr);
}

void DocumentPane::draw(cairo_t* cr) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_area, &allocation);

    cairo_set_source_rgb(cr, 0.906, 0.906, 0.894);
    cairo_paint(cr);

    // Render only pages intersecting the exposed clip region: the scrolled
    // window translates and clips the Cairo context to the visible viewport.
    GdkRectangle clip;
    if (!gdk_cairo_get_clip_rectangle(cr, &clip)) {
        clip.x = 0;
        clip.y = 0;
        clip.width = allocation.width;
        clip.height = allocation.height;
    }

    for (const PageLayout& layout : m_pages) {
        if (layout.y + layout.height < clip.y || layout.y > clip.y + clip.height)
            continue;

        cairo_save(cr);
        cairo_translate(cr, kPageMargin + std::max(0.0, (allocation.width - m_layoutWidth) / 2.0),
                        layout.y);

        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_rectangle(cr, 0.0, 0.0, layout.width, layout.height);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.70, 0.70, 0.68);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        // Screen-rendering overload (print pipeline lives in
        // poppler_page_render_for_printing); stable across releases.
        poppler_page_render(layout.page, cr);
        cairo_restore(cr);
    }
}

} // namespace FluidCoreApp
