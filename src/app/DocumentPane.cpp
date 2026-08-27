#include "DocumentPane.h"
#include "InkOverlay.h"
#include "ThumbnailLayout.h"
#include "ThumbnailSidebar.h"
#include "undo/AnnotationCommands.h"

#include <algorithm>
#include <vector>

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

DocumentPane::DocumentPane(const std::string& pdfPath) : m_pdfPath(pdfPath) {
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
    std::vector<ThumbnailSidebar::PageEntry> thumbPages;
    thumbPages.reserve(pageCount);

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
        m_annotationStore.setPageDimensions(static_cast<std::size_t>(i), layout.width,
                                            layout.height);
        m_pages.push_back(layout);
        thumbPages.push_back({page, layout.width, layout.height, layout.y});
    }
    m_layoutWidth += 2.0 * kPageMargin;
    m_layoutHeight = y;

    m_overlay = gtk_overlay_new();
    gtk_widget_set_size_request(m_overlay, static_cast<int>(m_layoutWidth),
                                static_cast<int>(m_layoutHeight));

    m_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_area, static_cast<int>(m_layoutWidth),
                                static_cast<int>(m_layoutHeight));
    g_signal_connect(m_area, "draw", G_CALLBACK(DocumentPane::drawCallback), this);
    gtk_container_add(GTK_CONTAINER(m_overlay), m_area);

    m_inkOverlay = std::make_unique<InkOverlay>(*this, m_annotationStore);
    gtk_overlay_add_overlay(GTK_OVERLAY(m_overlay), m_inkOverlay->widget());
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(m_overlay), m_inkOverlay->widget(), FALSE);

    gtk_container_add(GTK_CONTAINER(m_scroller), m_overlay);

    // Initialize thumbnail sidebar
    m_thumbnailSidebar = std::make_unique<ThumbnailSidebar>(thumbPages);
    m_thumbnailSidebar->setPageSelectedCallback(
        [this](std::size_t pageIdx) { scrollToPage(pageIdx); });

    // Host in horizontal GtkPaned with draggable divider
    m_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* thumbWidget = m_thumbnailSidebar->widget();
    gtk_widget_set_size_request(thumbWidget, 120, -1);
    gtk_paned_pack1(GTK_PANED(m_paned), thumbWidget, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(m_paned), m_scroller, TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(m_paned), 160);

    // Synchronize document scroll position to active thumbnail highlight
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
    if (vadj) {
        g_signal_connect(vadj, "value-changed", G_CALLBACK(DocumentPane::onScrollValueChanged),
                         this);
    }

    // Auto-load companion .xopp if present
    m_annotationStore.loadAnnotations(m_pdfPath);
}

DocumentPane::~DocumentPane() {
    // Clear undo stack first to prevent any dangling command pointers to stores
    m_undoStack.clear();
    if (!m_pdfPath.empty() && !m_annotationStore.strokes().empty()) {
        saveAnnotations();
    }
    m_pageTileCache.clear();
    m_thumbnailSidebar.reset();
    for (PageLayout& layout : m_pages) {
        if (layout.page)
            g_object_unref(layout.page);
    }
    if (m_document)
        g_object_unref(m_document);
}

bool DocumentPane::saveAnnotations() {
    if (m_pdfPath.empty()) {
        return false;
    }
    return m_annotationStore.saveAnnotations(m_pdfPath);
}

bool DocumentPane::undo() {
    if (!m_undoStack.canUndo()) {
        return false;
    }

    const FluidCore::Command* topCmd = m_undoStack.peekUndo();
    const auto* addCmd = dynamic_cast<const FluidCore::AddStrokeCommand*>(topCmd);
    const auto* remCmd = dynamic_cast<const FluidCore::RemoveStrokeCommand*>(topCmd);
    const auto* clrCmd = dynamic_cast<const FluidCore::ClearPageStrokesCommand*>(topCmd);
    const auto* compCmd = dynamic_cast<const FluidCore::CompoundCommand*>(topCmd);

    FluidCore::Stroke affectedStroke;
    bool hasStroke = false;
    std::size_t pageToInvalidate = 0;
    bool hasPage = false;

    if (addCmd) {
        affectedStroke = addCmd->stroke();
        hasStroke = true;
    } else if (remCmd) {
        affectedStroke = remCmd->stroke();
        hasStroke = true;
    } else if (clrCmd) {
        pageToInvalidate = clrCmd->pageIndex();
        hasPage = true;
    }

    const bool ok = m_undoStack.undo();

    if (ok && m_inkOverlay) {
        if (hasStroke) {
            m_inkOverlay->invalidateStroke(affectedStroke);
        } else if (hasPage) {
            m_inkOverlay->invalidatePage(pageToInvalidate);
        } else if (compCmd) {
            for (const auto& sub : compCmd->commands()) {
                if (const auto* sRem =
                        dynamic_cast<const FluidCore::RemoveStrokeCommand*>(sub.get())) {
                    m_inkOverlay->invalidateStroke(sRem->stroke());
                } else if (const auto* sAdd =
                               dynamic_cast<const FluidCore::AddStrokeCommand*>(sub.get())) {
                    m_inkOverlay->invalidateStroke(sAdd->stroke());
                }
            }
        }
    }
    return ok;
}

bool DocumentPane::redo() {
    if (!m_undoStack.canRedo()) {
        return false;
    }

    const FluidCore::Command* topCmd = m_undoStack.peekRedo();
    const auto* addCmd = dynamic_cast<const FluidCore::AddStrokeCommand*>(topCmd);
    const auto* remCmd = dynamic_cast<const FluidCore::RemoveStrokeCommand*>(topCmd);
    const auto* clrCmd = dynamic_cast<const FluidCore::ClearPageStrokesCommand*>(topCmd);
    const auto* compCmd = dynamic_cast<const FluidCore::CompoundCommand*>(topCmd);

    FluidCore::Stroke affectedStroke;
    bool hasStroke = false;
    std::size_t pageToInvalidate = 0;
    bool hasPage = false;

    if (addCmd) {
        affectedStroke = addCmd->stroke();
        hasStroke = true;
    } else if (remCmd) {
        affectedStroke = remCmd->stroke();
        hasStroke = true;
    } else if (clrCmd) {
        pageToInvalidate = clrCmd->pageIndex();
        hasPage = true;
    }

    const bool ok = m_undoStack.redo();

    if (ok && m_inkOverlay) {
        if (hasStroke) {
            m_inkOverlay->invalidateStroke(affectedStroke);
        } else if (hasPage) {
            m_inkOverlay->invalidatePage(pageToInvalidate);
        } else if (compCmd) {
            for (const auto& sub : compCmd->commands()) {
                if (const auto* sRem =
                        dynamic_cast<const FluidCore::RemoveStrokeCommand*>(sub.get())) {
                    m_inkOverlay->invalidateStroke(sRem->stroke());
                } else if (const auto* sAdd =
                               dynamic_cast<const FluidCore::AddStrokeCommand*>(sub.get())) {
                    m_inkOverlay->invalidateStroke(sAdd->stroke());
                }
            }
        }
    }
    return ok;
}

void DocumentPane::scrollToPage(std::size_t pageIndex) {
    if (pageIndex >= m_pages.size() || !m_scroller) {
        return;
    }

    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
    if (!vadj) {
        return;
    }

    const double targetY = std::max(0.0, m_pages[pageIndex].y - kPageMargin);
    gtk_adjustment_set_value(vadj, targetY);
}

void DocumentPane::onScrollValueChanged(GtkAdjustment* adj, gpointer userData) {
    auto* pane = static_cast<DocumentPane*>(userData);
    if (!pane || !pane->m_thumbnailSidebar || pane->m_pages.empty()) {
        return;
    }

    const double currentY = gtk_adjustment_get_value(adj);
    const double viewHeight = gtk_adjustment_get_page_size(adj);

    std::vector<ThumbnailLayout::PageDimension> dims;
    dims.reserve(pane->m_pages.size());
    for (const auto& p : pane->m_pages) {
        dims.push_back({p.width, p.height, p.y});
    }

    const std::size_t activeIdx = ThumbnailLayout::findActivePage(dims, currentY, viewHeight);
    pane->m_thumbnailSidebar->setActivePage(activeIdx);
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

    // Collect visible page indices to pin them in the PageTileCache (prevent eviction thrashing)
    std::vector<std::size_t> visiblePageIndices;
    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        const auto& layout = m_pages[i];
        if (layout.y + layout.height >= clip.y && layout.y <= clip.y + clip.height) {
            visiblePageIndices.push_back(i);
        }
    }
    m_pageTileCache.setPinnedPages(visiblePageIndices);

    const double pageX = kPageMargin + std::max(0.0, (allocation.width - m_layoutWidth) / 2.0);

    for (std::size_t i : visiblePageIndices) {
        const PageLayout& layout = m_pages[i];

        cairo_save(cr);
        cairo_translate(cr, pageX, layout.y);

        // Retrieve from LRU tile cache or render on-demand into cached image surface
        CairoSurfaceHandle surface = m_pageTileCache.get(i);
        if (!surface && layout.page) {
            surface = m_pageTileCache.renderPage(i, layout.page, layout.width, layout.height);
        }

        if (surface) {
            cairo_set_source_surface(cr, surface.get(), 0.0, 0.0);
            cairo_paint(cr);
        } else {
            // Fallback direct rasterization
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_rectangle(cr, 0.0, 0.0, layout.width, layout.height);
            cairo_fill(cr);
            if (layout.page) {
                poppler_page_render(layout.page, cr);
            }
        }

        // Page border
        cairo_set_source_rgb(cr, 0.70, 0.70, 0.68);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, 0.0, 0.0, layout.width, layout.height);
        cairo_stroke(cr);

        cairo_restore(cr);
    }
}

} // namespace FluidCoreApp
