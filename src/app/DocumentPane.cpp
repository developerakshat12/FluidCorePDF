#include "DocumentPane.h"
#include "InkOverlay.h"
#include "SqueezeRenderHelper.h"
#include "search/SearchSqueezePlanner.h"
#include "undo/AnnotationCommands.h"
#include "undo/SqueezeCommands.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <cairo.h>

namespace FluidCoreApp {
namespace {

constexpr double kPageMargin = 16.0;
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

    std::vector<FluidCore::PageGeometry> pageGeometries;
    pageGeometries.reserve(pageCount);

    for (int i = 0; i < pageCount; ++i) {
        PopplerPage* page = poppler_document_get_page(m_document, i);
        if (!page)
            continue;
        PageLayout layout;
        layout.page = page;
        poppler_page_get_size(page, &layout.width, &layout.height);
        layout.y = y;

        pageGeometries.push_back(
            FluidCore::PageGeometry{static_cast<std::size_t>(i), layout.width, layout.height, y});

        y += layout.height + kPageGap;
        m_layoutWidth = std::max(m_layoutWidth, layout.width);
        m_annotationStore.setPageDimensions(static_cast<std::size_t>(i), layout.width,
                                            layout.height);
        m_pages.push_back(layout);
    }
    m_layoutWidth += 2.0 * kPageMargin;
    m_layoutHeight = y;

    // Register full geometry with SqueezeEngine
    m_squeezeEngine.registerDocumentGeometry(m_docId, pageGeometries);

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

    // Embed floating SearchBarWidget overlay
    m_searchBar = std::make_unique<SearchBarWidget>();
    m_searchBar->setQueryChangedCallback([this](const std::string& query, bool enableSqueeze) {
        onSearchQueryChanged(query, enableSqueeze);
    });
    m_searchBar->setNavigateCallback([this](int dir) { navigateSearch(dir); });
    m_searchBar->setSqueezeToggleCallback([this](bool enableSqueeze) {
        onSearchSqueezeToggled(enableSqueeze);
    });
    m_searchBar->setCloseCallback([this]() { closeSearch(); });

    gtk_overlay_add_overlay(GTK_OVERLAY(m_overlay), m_searchBar->widget());
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(m_overlay), m_searchBar->widget(), FALSE);

    gtk_container_add(GTK_CONTAINER(m_scroller), m_overlay);

    // Event connections for desktop squeeze gestures
    gtk_widget_add_events(m_overlay, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK |
                                         GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(m_overlay, "scroll-event", G_CALLBACK(DocumentPane::scrollCallback), this);

    // Auto-load companion .xopp if present
    m_annotationStore.loadAnnotations(m_pdfPath);
}

DocumentPane::~DocumentPane() {
    if (m_wheelDebounceTimerId != 0) {
        g_source_remove(m_wheelDebounceTimerId);
        m_wheelDebounceTimerId = 0;
    }
    m_searchService.cancel();
    clearTextSelection();
    if (m_inkOverlay) {
        m_inkOverlay->textSelectionService().clearCache();
    }
    m_undoStack.clear();
    if (!m_pdfPath.empty() && !m_annotationStore.strokes().empty()) {
        saveAnnotations();
    }
    m_pageTileCache.clear();
    for (PageLayout& layout : m_pages) {
        if (layout.page)
            g_object_unref(layout.page);
    }
    if (m_document)
        g_object_unref(m_document);
}

double DocumentPane::docYToScreen(double docY) const {
    return m_squeezeEngine.mapDocumentYToScreen(docY, m_docId).screenY;
}

double DocumentPane::screenYToDoc(double screenY) const {
    return m_squeezeEngine.mapScreenYToDocument(screenY, m_docId).screenY;
}

std::vector<FluidCore::SqueezeSegment> DocumentPane::squeezeSegments() const {
    return m_squeezeEngine.getSegments(m_docId);
}

double DocumentPane::totalSqueezedHeight() const {
    return m_squeezeEngine.totalSqueezedHeight(m_docId);
}

bool DocumentPane::isSqueezed() const {
    const auto& segs = m_squeezeEngine.getSegments(m_docId);
    for (const auto& s : segs) {
        if (s.alpha < 0.999) {
            return true;
        }
    }
    return false;
}

void DocumentPane::updateLayoutDimensions() {
    const double squeezedH = m_squeezeEngine.totalSqueezedHeight(m_docId) + 24.0;
    m_layoutHeight = squeezedH;

    if (m_overlay) {
        gtk_widget_set_size_request(m_overlay, static_cast<int>(m_layoutWidth),
                                    static_cast<int>(m_layoutHeight));
    }
    if (m_area) {
        gtk_widget_set_size_request(m_area, static_cast<int>(m_layoutWidth),
                                    static_cast<int>(m_layoutHeight));
        gtk_widget_queue_draw(m_area);
    }
    if (m_inkOverlay && m_inkOverlay->widget()) {
        gtk_widget_set_size_request(m_inkOverlay->widget(), static_cast<int>(m_layoutWidth),
                                    static_cast<int>(m_layoutHeight));
        gtk_widget_queue_draw(m_inkOverlay->widget());
    }
}

void DocumentPane::setSqueezeFold(double yStart, double yEnd, double alpha) {
    auto regions = m_squeezeEngine.getRawRegions(m_docId);
    regions.push_back(FluidCore::SqueezeRegion{"", yStart, yEnd, alpha});
    m_undoStack.pushAndExecute(std::make_unique<FluidCore::SetSqueezeRegionsCommand>(
        m_squeezeEngine, m_docId, std::move(regions)));
    updateLayoutDimensions();
}

void DocumentPane::resetSqueeze() {
    m_undoStack.pushAndExecute(
        std::make_unique<FluidCore::ResetSqueezeCommand>(m_squeezeEngine, m_docId));
    updateLayoutDimensions();
}

void DocumentPane::openSearch(bool enableSqueeze) {
    if (m_searchBar) {
        m_searchBar->show(enableSqueeze);
        if (!m_searchBar->currentQuery().empty()) {
            onSearchQueryChanged(m_searchBar->currentQuery(), enableSqueeze);
        }
    }
}

void DocumentPane::closeSearch() {
    if (m_searchBar) {
        m_searchBar->hide();
    }
    m_searchService.cancel();
    m_searchHits.clear();
    m_squeezeEngine.clearSearchSqueeze(m_docId);
    updateLayoutDimensions();
}

bool DocumentPane::isSearchActive() const {
    return m_searchBar && m_searchBar->isVisible();
}

void DocumentPane::onSearchQueryChanged(const std::string& query, bool enableSqueeze) {
    if (query.empty()) {
        m_searchHits.clear();
        if (m_searchBar) {
            m_searchBar->setMatchStatus(0, 0);
        }
        m_squeezeEngine.clearSearchSqueeze(m_docId);
        updateLayoutDimensions();
        return;
    }

    m_searchService.searchAsync(
        m_document, m_pages, query,
        [this, enableSqueeze](std::vector<SearchHit> hits) {
            m_searchHits = std::move(hits);
            m_activeSearchHitIndex = 0;
            if (m_searchBar) {
                m_searchBar->setMatchStatus(m_activeSearchHitIndex, m_searchHits.size());
            }

            if (enableSqueeze && !m_searchHits.empty()) {
                applySearchSqueeze();
            } else {
                m_squeezeEngine.clearSearchSqueeze(m_docId);
                updateLayoutDimensions();
            }

            if (!m_searchHits.empty()) {
                scrollToSearchHit(0);
            }
        });
}

void DocumentPane::onSearchSqueezeToggled(bool enableSqueeze) {
    if (enableSqueeze && !m_searchHits.empty()) {
        applySearchSqueeze();
    } else {
        m_squeezeEngine.clearSearchSqueeze(m_docId);
        updateLayoutDimensions();
    }
}

void DocumentPane::applySearchSqueeze() {
    if (m_pages.empty() || m_searchHits.empty()) {
        return;
    }

    const double totalDocH = m_pages.back().y + m_pages.back().height;
    auto hitSpans = DocumentSearchService::toHitSpans(m_searchHits);

    FluidCore::SearchSqueezeConfig config;
    config.contextPadding = 40.0;
    config.gapAlpha = 0.08;

    auto regions =
        FluidCore::SearchSqueezePlanner::computeSearchSqueezeRegions(totalDocH, hitSpans, config);

    m_squeezeEngine.setSearchSqueezeRegions(m_docId, std::move(regions));
    updateLayoutDimensions();
}

void DocumentPane::navigateSearch(int direction) {
    if (m_searchHits.empty()) {
        return;
    }

    if (direction > 0) {
        m_activeSearchHitIndex = (m_activeSearchHitIndex + 1) % m_searchHits.size();
    } else {
        m_activeSearchHitIndex =
            (m_activeSearchHitIndex + m_searchHits.size() - 1) % m_searchHits.size();
    }

    if (m_searchBar) {
        m_searchBar->setMatchStatus(m_activeSearchHitIndex, m_searchHits.size());
    }

    scrollToSearchHit(m_activeSearchHitIndex);
    if (m_area) {
        gtk_widget_queue_draw(m_area);
    }
}

void DocumentPane::scrollToSearchHit(std::size_t hitIndex) {
    if (hitIndex >= m_searchHits.size()) {
        return;
    }

    const double targetScreenY = docYToScreen(m_searchHits[hitIndex].docYStart);
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
    if (vadj) {
        const double pageSize = gtk_adjustment_get_page_size(vadj);
        gtk_adjustment_set_value(vadj, std::max(0.0, targetScreenY - pageSize * 0.35));
    }
}

bool DocumentPane::hasTextSelection() const {
    return m_inkOverlay ? m_inkOverlay->hasSelection() : false;
}

void DocumentPane::clearTextSelection() {
    if (m_inkOverlay) {
        m_inkOverlay->clearSelection();
    }
}

bool DocumentPane::copySelection() {
    return m_inkOverlay ? m_inkOverlay->copySelection() : false;
}

void DocumentPane::setTool(const std::string& tool) {
    if (m_inkOverlay) {
        m_inkOverlay->setTool(tool);
    }
}

const std::string& DocumentPane::tool() const {
    static const std::string s_empty;
    return m_inkOverlay ? m_inkOverlay->tool() : s_empty;
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
    const bool isSqueezeCmd =
        (dynamic_cast<const FluidCore::SetSqueezeRegionsCommand*>(topCmd) != nullptr ||
         dynamic_cast<const FluidCore::ResetSqueezeCommand*>(topCmd) != nullptr);

    const bool ok = m_undoStack.undo();
    if (ok) {
        if (isSqueezeCmd) {
            updateLayoutDimensions();
        } else {
            if (m_inkOverlay && m_inkOverlay->widget()) {
                gtk_widget_queue_draw(m_inkOverlay->widget());
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
    const bool isSqueezeCmd =
        (dynamic_cast<const FluidCore::SetSqueezeRegionsCommand*>(topCmd) != nullptr ||
         dynamic_cast<const FluidCore::ResetSqueezeCommand*>(topCmd) != nullptr);

    const bool ok = m_undoStack.redo();
    if (ok) {
        if (isSqueezeCmd) {
            updateLayoutDimensions();
        } else {
            if (m_inkOverlay && m_inkOverlay->widget()) {
                gtk_widget_queue_draw(m_inkOverlay->widget());
            }
        }
    }
    return ok;
}

void DocumentPane::drawCallback(GtkWidget*, cairo_t* cr, gpointer userData) {
    static_cast<DocumentPane*>(userData)->draw(cr);
}

gboolean DocumentPane::scrollCallback(GtkWidget*, GdkEventScroll* event, gpointer userData) {
    return static_cast<DocumentPane*>(userData)->onScroll(event);
}

gboolean DocumentPane::onScroll(GdkEventScroll* event) {
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    const bool shift = (event->state & GDK_SHIFT_MASK) != 0;

    if (!ctrl || !shift) {
        return FALSE;
    }

    const double docY = screenYToDoc(event->y);

    if (!m_hasActiveWheelSqueeze) {
        m_hasActiveWheelSqueeze = true;
        m_wheelSqueezeCenterDocY = docY;
        m_wheelSqueezeAlpha = 1.0;
    }

    double delta = 0.0;
    if (event->direction == GDK_SCROLL_UP) {
        delta = -1.0;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        delta = 1.0;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        delta = event->delta_y;
    }

    if (delta != 0.0) {
        if (delta > 0) {
            m_wheelSqueezeAlpha = std::clamp(m_wheelSqueezeAlpha * 0.85, 0.05, 1.0);
        } else {
            m_wheelSqueezeAlpha = std::clamp(m_wheelSqueezeAlpha * 1.20, 0.05, 1.0);
        }

        const double span = 140.0;
        const double y0 = std::max(0.0, m_wheelSqueezeCenterDocY - span);
        const double y1 = m_wheelSqueezeCenterDocY + span;

        m_squeezeEngine.setPreviewSqueezeRegion(m_docId, y0, y1, m_wheelSqueezeAlpha);
        updateLayoutDimensions();

        if (m_wheelDebounceTimerId != 0) {
            g_source_remove(m_wheelDebounceTimerId);
        }
        m_wheelDebounceTimerId = g_timeout_add(
            300,
            +[](gpointer data) -> gboolean {
                auto* self = static_cast<DocumentPane*>(data);
                self->commitScrollSqueeze();
                self->m_wheelDebounceTimerId = 0;
                return G_SOURCE_REMOVE;
            },
            this);

        return TRUE;
    }

    return FALSE;
}

void DocumentPane::commitScrollSqueeze() {
    if (!m_hasActiveWheelSqueeze) {
        return;
    }

    const double span = 140.0;
    const double y0 = std::max(0.0, m_wheelSqueezeCenterDocY - span);
    const double y1 = m_wheelSqueezeCenterDocY + span;
    const double alpha = m_wheelSqueezeAlpha;

    m_hasActiveWheelSqueeze = false;
    m_squeezeEngine.clearPreviewSqueezeRegion(m_docId);

    if (alpha < 0.99) {
        setSqueezeFold(y0, y1, alpha);
    } else {
        updateLayoutDimensions();
    }
}

void DocumentPane::draw(cairo_t* cr) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(m_area, &allocation);

    cairo_set_source_rgb(cr, 0.906, 0.906, 0.894);
    cairo_paint(cr);

    GdkRectangle clip;
    if (!gdk_cairo_get_clip_rectangle(cr, &clip)) {
        clip.x = 0;
        clip.y = 0;
        clip.width = allocation.width;
        clip.height = allocation.height;
    }

    const auto segments = m_squeezeEngine.getSegments(m_docId);
    const double pageX = kPageMargin + std::max(0.0, (allocation.width - m_layoutWidth) / 2.0);

    std::vector<std::size_t> visiblePageIndices;

    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        const PageLayout& layout = m_pages[i];
        auto slices = SqueezeRenderHelper::decomposePage(i, layout.y, layout.height, segments);

        bool pageVisible = false;
        for (const auto& slice : slices) {
            if (slice.screenYEnd < clip.y || slice.screenYStart > clip.y + clip.height) {
                continue;
            }
            pageVisible = true;
            break;
        }

        if (pageVisible) {
            visiblePageIndices.push_back(i);
        }
    }
    m_pageTileCache.setPinnedPages(visiblePageIndices);

    for (std::size_t i : visiblePageIndices) {
        const PageLayout& layout = m_pages[i];
        auto slices = SqueezeRenderHelper::decomposePage(i, layout.y, layout.height, segments);

        CairoSurfaceHandle surface = m_pageTileCache.get(i);
        if (!surface && layout.page) {
            surface = m_pageTileCache.renderPage(i, layout.page, layout.width, layout.height);
        }

        for (const auto& slice : slices) {
            if (slice.screenYEnd < clip.y || slice.screenYStart > clip.y + clip.height) {
                continue;
            }

            cairo_save(cr);
            cairo_rectangle(cr, pageX, slice.screenYStart, layout.width,
                            slice.screenYEnd - slice.screenYStart);
            cairo_clip(cr);

            if (surface) {
                const double yOffset = slice.screenYStart - slice.pageLocalDocYStart;
                cairo_set_source_surface(cr, surface.get(), pageX, yOffset);
                cairo_paint(cr);
            } else {
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                cairo_rectangle(cr, pageX, slice.screenYStart, layout.width,
                                slice.screenYEnd - slice.screenYStart);
                cairo_fill(cr);
            }

            // Render search hit highlights on this page slice
            if (!m_searchHits.empty()) {
                const double yOffset = slice.screenYStart - slice.pageLocalDocYStart;
                for (std::size_t hIdx = 0; hIdx < m_searchHits.size(); ++hIdx) {
                    const auto& hit = m_searchHits[hIdx];
                    if (hit.pageIndex != i) {
                        continue;
                    }

                    const double rY0 = hit.pageBounds.y1;
                    const double rY1 = hit.pageBounds.y2;
                    const double rX0 = hit.pageBounds.x1;
                    const double rX1 = hit.pageBounds.x2;

                    if (rY1 < slice.pageLocalDocYStart || rY0 > slice.pageLocalDocYEnd) {
                        continue;
                    }

                    const bool isActive = (hIdx == m_activeSearchHitIndex);
                    cairo_save(cr);

                    if (isActive) {
                        cairo_set_source_rgba(cr, 1.0, 0.55, 0.0, 0.45);
                    } else {
                        cairo_set_source_rgba(cr, 1.0, 0.85, 0.15, 0.35);
                    }

                    cairo_rectangle(cr, pageX + rX0 - 2.0, yOffset + rY0 - 1.0,
                                    (rX1 - rX0) + 4.0, (rY1 - rY0) + 2.0);
                    cairo_fill_preserve(cr);

                    if (isActive) {
                        cairo_set_source_rgb(cr, 1.0, 0.40, 0.0);
                        cairo_set_line_width(cr, 1.5);
                    } else {
                        cairo_set_source_rgb(cr, 0.85, 0.65, 0.0);
                        cairo_set_line_width(cr, 0.8);
                    }
                    cairo_stroke(cr);

                    cairo_restore(cr);
                }
            }

            // Slice boundary outline
            cairo_set_source_rgb(cr, 0.70, 0.70, 0.68);
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, pageX, slice.screenYStart, layout.width,
                            slice.screenYEnd - slice.screenYStart);
            cairo_stroke(cr);

            cairo_restore(cr);

            // Accordion crease visual overlay if folded
            if (slice.isCompressed) {
                SqueezeRenderHelper::renderAccordionCrease(cr, pageX, slice.screenYStart,
                                                          layout.width, slice.alpha);
            }
        }
    }

    // Render margin fold pins
    if (!m_squeezeEngine.isSearchSqueezeActive(m_docId)) {
        const auto rawRegions = m_squeezeEngine.getRawRegions(m_docId);
        for (const auto& r : rawRegions) {
            const double sy0 = m_squeezeEngine.mapDocumentYToScreen(r.yStart, m_docId).screenY;
            const double sy1 = m_squeezeEngine.mapDocumentYToScreen(r.yEnd, m_docId).screenY;
            SqueezeRenderHelper::renderMarginFoldPin(cr, pageX - 8.0, sy0, 4.5, false, false);
            SqueezeRenderHelper::renderMarginFoldPin(cr, pageX - 8.0, sy1, 4.5, false, false);
        }
    }
}

} // namespace FluidCoreApp
