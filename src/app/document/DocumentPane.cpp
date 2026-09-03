#include "document/DocumentPane.h"
#include "FluidCoreAPI.h"
#include "document/InkOverlay.h"
#include "document/SqueezeRenderHelper.h"
#include "search/AnchorSqueezePlanner.h"
#include "search/SearchSqueezePlanner.h"
#include "undo/AnnotationCommands.h"
#include "undo/SqueezeCommands.h"
#include "workspace/WorkspaceView.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
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

void drawRoundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (w <= 0.0 || h <= 0.0)
        return;
    r = std::min(r, std::min(w / 2.0, h / 2.0));
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

} // namespace

DocumentPane::DocumentPane(const std::string& pdfPath) : m_pdfPath(pdfPath) {
    m_viewOverlay = gtk_overlay_new();
    m_scroller = gtk_scrolled_window_new(nullptr, nullptr);
    g_signal_connect(m_scroller, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* self = static_cast<DocumentPane*>(data);
        if (self) {
            self->m_scroller = nullptr;
        }
    }), this);
    g_signal_connect(m_viewOverlay, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* self = static_cast<DocumentPane*>(data);
        if (self) {
            self->m_viewOverlay = nullptr;
            self->m_scroller = nullptr;
        }
    }), this);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroller), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(m_viewOverlay), m_scroller);

    // Embed floating SearchBarWidget overlay pinned to top-right
    m_searchBar = std::make_unique<SearchBarWidget>();
    m_searchBar->setQueryChangedCallback([this](const std::string& query, bool enableSqueeze) {
        onSearchQueryChanged(query, enableSqueeze);
    });
    m_searchBar->setNavigateCallback([this](int dir) { navigateSearch(dir); });
    m_searchBar->setSqueezeToggleCallback(
        [this](bool enableSqueeze) { onSearchSqueezeToggled(enableSqueeze); });
    m_searchBar->setScopeChangedCallback([this](SearchScope) {
        onSearchQueryChanged(m_searchBar->currentQuery(), m_searchBar->isSqueezeEnabled());
    });
    m_searchBar->setCloseCallback([this]() { closeSearch(); });

    GtkWidget* searchWidget = m_searchBar->widget();
    gtk_widget_set_halign(searchWidget, GTK_ALIGN_END);
    gtk_widget_set_valign(searchWidget, GTK_ALIGN_START);
    gtk_widget_set_margin_top(searchWidget, 16);
    gtk_widget_set_margin_end(searchWidget, 32);
    gtk_overlay_add_overlay(GTK_OVERLAY(m_viewOverlay), searchWidget);

    // Embed floating ReturnAnchorPill overlay centered at top of document viewport
    m_returnAnchorPill = std::make_unique<ReturnAnchorPill>();
    m_returnAnchorPill->setOnReturnClicked(
        [this](const FluidCore::Point& originPt, const std::string& cardId) {
            if (m_onReturnToWorkspace) {
                m_onReturnToWorkspace(originPt, cardId);
            }
        });

    GtkWidget* pillWidget = m_returnAnchorPill->widget();
    gtk_widget_set_halign(pillWidget, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(pillWidget, GTK_ALIGN_START);
    gtk_widget_set_margin_top(pillWidget, 16);
    gtk_overlay_add_overlay(GTK_OVERLAY(m_viewOverlay), pillWidget);

    loadDocument(pdfPath);
}

void DocumentPane::closeDocument() {
    if (m_pinchGesture) {
        g_object_unref(m_pinchGesture);
        m_pinchGesture = nullptr;
    }
    m_searchService.cancel();
    clearTextSelection();
    clearCropSelection();
    if (m_inkOverlay) {
        m_inkOverlay->textSelectionService().clearCache();
    }
    m_undoStack.clear();
    m_pageTileCache.clear();
    if (m_squeezeEngine.hasDocument(m_docId)) {
        m_squeezeEngine.resetSqueeze(m_docId);
    }
    m_searchHits.clear();
    m_excerptAnchors.clear();

    if (m_scroller && GTK_IS_WIDGET(m_scroller) && GTK_IS_BIN(m_scroller)) {
        GtkWidget* child = gtk_bin_get_child(GTK_BIN(m_scroller));
        if (child) {
            gtk_container_remove(GTK_CONTAINER(m_scroller), child);
        }
    }
    m_overlay = nullptr;
    m_area = nullptr;
    m_inkOverlay.reset();

    for (PageLayout& layout : m_pages) {
        if (layout.page) {
            g_object_unref(layout.page);
            layout.page = nullptr;
        }
    }
    m_pages.clear();
    if (m_document) {
        g_object_unref(m_document);
        m_document = nullptr;
    }
    m_pdfPath.clear();
    m_layoutWidth = 0.0;
    m_layoutHeight = 0.0;
}

void DocumentPane::repointCompanionPath(const std::string& newPdfPath) {
    m_pdfPath = newPdfPath;
}

bool DocumentPane::loadDocument(const std::string& pdfPath, const std::string& docId) {
    closeDocument();

    if (!docId.empty()) {
        m_docId = docId;
    } else if (!pdfPath.empty()) {
        std::filesystem::path p(pdfPath);
        m_docId = p.filename().string();
    } else {
        m_docId = "doc-primary";
    }

    m_pdfPath = pdfPath;
    if (pdfPath.empty()) {
        gtk_container_add(
            GTK_CONTAINER(m_scroller),
            makeStatusLabel(
                "No document loaded — open a PDF or project from the HeaderBar (Ctrl+O)."));
        gtk_widget_show_all(m_scroller);
        return true;
    }

    GError* error = nullptr;
    gchar* uri = nullptr;
    if (pdfPath.rfind("file://", 0) == 0) {
        uri = g_strdup(pdfPath.c_str());
    } else {
        uri = g_filename_to_uri(pdfPath.c_str(), nullptr, &error);
    }

    m_document = poppler_document_new_from_file(uri, nullptr, &error);
    g_free(uri);
    if (!m_document) {
        gchar* message = g_strdup_printf("Could not open PDF:\n%s\n\n(%s)", pdfPath.c_str(),
                                         error ? error->message : "unknown error");
        if (error)
            g_error_free(error);
        gtk_container_add(GTK_CONTAINER(m_scroller), makeStatusLabel(message));
        g_free(message);
        gtk_widget_show_all(m_scroller);
        return false;
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
    const int scaledWidth = static_cast<int>(m_layoutWidth * m_zoom);
    const int scaledHeight = static_cast<int>(m_layoutHeight * m_zoom);
    gtk_widget_set_size_request(m_overlay, scaledWidth, scaledHeight);

    m_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_area, scaledWidth, scaledHeight);
    g_signal_connect(m_area, "draw", G_CALLBACK(DocumentPane::drawCallback), this);
    gtk_container_add(GTK_CONTAINER(m_overlay), m_area);

    m_inkOverlay = std::make_unique<InkOverlay>(*this, m_annotationStore);
    gtk_overlay_add_overlay(GTK_OVERLAY(m_overlay), m_inkOverlay->widget());
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(m_overlay), m_inkOverlay->widget(), FALSE);

    gtk_container_add(GTK_CONTAINER(m_scroller), m_overlay);

    // Event connections for desktop squeeze gestures
    gtk_widget_add_events(m_overlay, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK |
                                         GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(m_overlay, "scroll-event", G_CALLBACK(DocumentPane::scrollCallback), this);

    // Attach touch pinch gesture recognizer to scroller with bubble phase
    m_pinchGesture = gtk_gesture_zoom_new(m_scroller);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(m_pinchGesture),
                                               GTK_PHASE_BUBBLE);
    g_signal_connect(m_pinchGesture, "scale-changed",
                     G_CALLBACK((+[](GtkGestureZoom*, gdouble scale, gpointer data) {
                         auto* self = static_cast<DocumentPane*>(data);
                         if (!self)
                             return;
                         double cx = 0.0, cy = 0.0;
                         gtk_gesture_get_bounding_box_center(GTK_GESTURE(self->m_pinchGesture), &cx,
                                                             &cy);
                         double delta = (scale < 1.0) ? 1.0 : -1.0;
                         self->applyContinuousSqueezeDelta(delta, cy);
                     })),
                     this);

    // Auto-load companion .xopp if present
    m_annotationStore.loadAnnotations(m_pdfPath);

    gtk_widget_show_all(m_scroller);
    return true;
}

DocumentPane::~DocumentPane() {
    if (m_pulseTimerId != 0) {
        g_source_remove(m_pulseTimerId);
        m_pulseTimerId = 0;
    }
    if (m_wheelDebounceTimerId != 0) {
        g_source_remove(m_wheelDebounceTimerId);
        m_wheelDebounceTimerId = 0;
    }
    if (m_zoomDebounceTimerId != 0) {
        g_source_remove(m_zoomDebounceTimerId);
        m_zoomDebounceTimerId = 0;
    }
    closeDocument();
}

double DocumentPane::docYToScreen(double docY) const {
    if (!m_squeezeEngine.hasDocument(m_docId)) {
        return docY * m_zoom;
    }
    return m_squeezeEngine.mapDocumentYToScreen(docY, m_docId).screenY * m_zoom;
}

double DocumentPane::screenYToDoc(double screenY) const {
    if (!m_squeezeEngine.hasDocument(m_docId)) {
        return screenY / m_zoom;
    }
    return m_squeezeEngine.mapScreenYToDocument(screenY / m_zoom, m_docId).screenY;
}

std::vector<FluidCore::SqueezeSegment> DocumentPane::squeezeSegments() const {
    if (!m_squeezeEngine.hasDocument(m_docId)) {
        return {};
    }
    return m_squeezeEngine.getSegments(m_docId);
}

double DocumentPane::totalSqueezedHeight() const {
    if (!m_squeezeEngine.hasDocument(m_docId)) {
        return m_layoutHeight;
    }
    return m_squeezeEngine.totalSqueezedHeight(m_docId);
}

bool DocumentPane::isSqueezed() const {
    if (!m_squeezeEngine.hasDocument(m_docId)) {
        return false;
    }
    const auto& segs = m_squeezeEngine.getSegments(m_docId);
    for (const auto& s : segs) {
        if (s.alpha < 0.999) {
            return true;
        }
    }
    return false;
}

void DocumentPane::updateLayoutDimensions() {
    double baseH = m_layoutHeight;
    if (m_squeezeEngine.hasDocument(m_docId)) {
        baseH = m_squeezeEngine.totalSqueezedHeight(m_docId);
    }
    const double squeezedH = baseH + 24.0;
    m_layoutHeight = squeezedH;

    const int scaledWidth = static_cast<int>(m_layoutWidth * m_zoom);
    const int scaledHeight = static_cast<int>(m_layoutHeight * m_zoom);

    if (m_overlay) {
        gtk_widget_set_size_request(m_overlay, scaledWidth, scaledHeight);
    }
    if (m_area) {
        gtk_widget_set_size_request(m_area, scaledWidth, scaledHeight);
        gtk_widget_queue_draw(m_area);
    }
    if (m_inkOverlay && m_inkOverlay->widget()) {
        gtk_widget_set_size_request(m_inkOverlay->widget(), scaledWidth, scaledHeight);
        gtk_widget_queue_draw(m_inkOverlay->widget());
    }
}

void DocumentPane::setZoom(double zoom) {
    const double oldZoom = m_zoom;
    m_zoom = std::clamp(zoom, 0.5, 2.0);

    if (m_zoom == oldZoom) {
        return;
    }

    m_isZooming = true;
    updateLayoutDimensions();

    if (m_zoomDebounceTimerId != 0) {
        g_source_remove(m_zoomDebounceTimerId);
    }

    m_zoomDebounceTimerId = g_timeout_add(
        150,
        [](gpointer data) -> gboolean {
            auto* pane = static_cast<DocumentPane*>(data);
            pane->commitZoom();
            return G_SOURCE_REMOVE;
        },
        this);
}

void DocumentPane::commitZoom() {
    m_zoomDebounceTimerId = 0;
    m_isZooming = false;
    clearCache();
    if (m_area)
        gtk_widget_queue_draw(m_area);
}

void DocumentPane::zoomIn() {
    setZoom(m_zoom * 1.2);
}

void DocumentPane::zoomOut() {
    setZoom(m_zoom / 1.2);
}

void DocumentPane::resetZoom() {
    setZoom(1.0);
}

void DocumentPane::setSqueezeFold(double yStart, double yEnd, double alpha) {
    auto regions = m_squeezeEngine.getRawRegions(m_docId);
    regions.push_back(FluidCore::SqueezeRegion{"", yStart, yEnd, alpha});
    m_undoStack.pushAndExecute(std::make_unique<FluidCore::SetSqueezeRegionsCommand>(
        m_squeezeEngine, m_docId, std::move(regions)));
    updateLayoutDimensions();
}

void DocumentPane::resetSqueeze() {
    m_highlightViewActive = false;
    m_searchSqueezeAlpha = 0.04;
    m_highlightSqueezeAlpha = 1.0;
    m_activeFoldSpan = 0.0;
    m_activeFoldStartDocY = 0.0;
    m_activeFoldRegionId.clear();
    m_hasActiveFoldGesture = false;

    m_squeezeEngine.clearSearchSqueeze(m_docId);
    m_squeezeEngine.clearHighlightSqueeze(m_docId);
    m_squeezeEngine.clearPreviewSqueezeRegion(m_docId);

    m_undoStack.pushAndExecute(
        std::make_unique<FluidCore::ResetSqueezeCommand>(m_squeezeEngine, m_docId));
    updateLayoutDimensions();
}

void DocumentPane::setHighlightView(bool enable) {
    m_highlightViewActive = enable;
    if (enable) {
        m_highlightSqueezeAlpha = 0.04;
        applyHighlightSqueeze();
    } else {
        m_highlightSqueezeAlpha = 1.0;
        m_squeezeEngine.clearHighlightSqueeze(m_docId);
        updateLayoutDimensions();
    }
}

void DocumentPane::toggleHighlightView() {
    setHighlightView(!m_highlightViewActive);
}

void DocumentPane::setExcerptAnchors(std::vector<FluidCore::AnchorSpan> anchors) {
    m_excerptAnchors = std::move(anchors);
    if (m_highlightViewActive) {
        applyHighlightSqueeze();
    }
}

void DocumentPane::addExcerptAnchor(const FluidCore::ExcerptCardNode& card) {
    if (card.sourcePageNo() >= m_pages.size()) {
        return;
    }
    const auto& page = m_pages[card.sourcePageNo()];
    const auto& srcRect = card.sourceNormalizedRect();
    const double y0 = page.y + srcRect.y * page.height;
    const double y1 = y0 + srcRect.h * page.height;
    m_excerptAnchors.push_back(FluidCore::AnchorSpan{y0, y1, "excerpt", 9});
    if (m_highlightViewActive) {
        applyHighlightSqueeze();
    }
}

std::vector<FluidCore::AnchorSpan> DocumentPane::collectActiveAnchors(double cursorDocY) const {
    std::vector<FluidCore::AnchorSpan> anchors;

    // Source 1: Active Search Matches
    if (!m_searchHits.empty()) {
        for (const auto& hit : m_searchHits) {
            anchors.push_back(FluidCore::AnchorSpan{hit.docYStart, hit.docYEnd, "search", 10});
        }
    }

    // Source 2: Highlights and Annotations
    if (m_highlightViewActive || !m_annotationStore.strokes().empty()) {
        for (const auto& s : m_annotationStore.strokes()) {
            if (s.pageIndex >= m_pages.size() || s.points.empty()) {
                continue;
            }
            const double pageTopY = m_pages[s.pageIndex].y;
            double minY = s.points.front().y;
            double maxY = s.points.front().y;
            for (const auto& pt : s.points) {
                minY = std::min(minY, pt.y);
                maxY = std::max(maxY, pt.y);
            }
            anchors.push_back(
                FluidCore::AnchorSpan{pageTopY + minY, pageTopY + maxY, "highlight", 8});
        }
    }

    // Source 3: Excerpt Document Source Anchors
    for (const auto& ea : m_excerptAnchors) {
        anchors.push_back(ea);
    }

    // Source 4: Cursor Fold Anchor (when no search or highlight filters active)
    if (anchors.empty() && cursorDocY >= 0.0) {
        const double span = 120.0;
        anchors.push_back(FluidCore::AnchorSpan{std::max(0.0, cursorDocY - span), cursorDocY + span,
                                                "cursor", 5});
    }

    return anchors;
}

void DocumentPane::applyHighlightSqueeze() {
    if (m_pages.empty()) {
        return;
    }

    const double totalDocH = m_pages.back().y + m_pages.back().height;
    auto anchors = collectActiveAnchors();

    if (anchors.empty()) {
        m_squeezeEngine.clearHighlightSqueeze(m_docId);
        updateLayoutDimensions();
        return;
    }

    FluidCore::AnchorSqueezeConfig config;
    config.contextPadding = 32.0;
    config.gapAlpha = m_highlightSqueezeAlpha;
    config.minGapHeight = 16.0;

    auto regions =
        FluidCore::AnchorSqueezePlanner::computeAnchorSqueezeRegions(totalDocH, anchors, config);

    m_squeezeEngine.setHighlightSqueezeRegions(m_docId, std::move(regions));
    updateLayoutDimensions();
}

void DocumentPane::openSearch(bool enableSqueeze, SearchScope scope) {
    if (m_searchBar) {
        m_searchBar->show(enableSqueeze, scope);
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
    if (m_workspaceView) {
        m_workspaceView->clearSearch();
    }
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
        if (m_workspaceView) {
            m_workspaceView->clearSearch();
        }
        updateLayoutDimensions();
        return;
    }

    const SearchScope scope = m_searchBar ? m_searchBar->currentScope() : SearchScope::Document;

    if (scope == SearchScope::Workspace) {
        m_searchHits.clear();
        m_squeezeEngine.clearSearchSqueeze(m_docId);
        updateLayoutDimensions();

        if (m_coreApi && m_workspaceView) {
            auto wsMatches = m_coreApi->searchWorkspace(query);
            m_workspaceView->setSearchResults(wsMatches, query, 0);
            if (m_searchBar) {
                m_searchBar->setMatchStatus(
                    wsMatches.empty() ? 0 : m_workspaceView->activeSearchMatchIndex(),
                    wsMatches.size());
            }
        }
        return;
    }

    if (scope == SearchScope::Document) {
        if (m_workspaceView) {
            m_workspaceView->clearSearch();
        }

        m_searchService.searchAsync(
            m_document, m_pages, query, [this, enableSqueeze](std::vector<SearchHit> hits) {
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
        return;
    }

    if (scope == SearchScope::All) {
        std::vector<FluidCore::WorkspaceMatch> wsMatches;
        if (m_coreApi && m_workspaceView) {
            wsMatches = m_coreApi->searchWorkspace(query);
            m_workspaceView->setSearchResults(wsMatches, query, 0);
        }

        const std::size_t wsCount = wsMatches.size();

        m_searchService.searchAsync(m_document, m_pages, query,
                                    [this, enableSqueeze, wsCount](std::vector<SearchHit> hits) {
                                        m_searchHits = std::move(hits);
                                        m_activeSearchHitIndex = 0;
                                        const std::size_t total = m_searchHits.size() + wsCount;

                                        if (m_searchBar) {
                                            m_searchBar->setScopedMatchStatus(
                                                m_activeSearchHitIndex, total, m_searchHits.size(),
                                                wsCount);
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
    config.gapAlpha = m_searchSqueezeAlpha;

    auto regions =
        FluidCore::SearchSqueezePlanner::computeSearchSqueezeRegions(totalDocH, hitSpans, config);

    m_squeezeEngine.setSearchSqueezeRegions(m_docId, std::move(regions));
    updateLayoutDimensions();
}

void DocumentPane::navigateSearch(int direction) {
    const SearchScope scope = m_searchBar ? m_searchBar->currentScope() : SearchScope::Document;

    if (scope == SearchScope::Workspace) {
        if (m_workspaceView) {
            m_workspaceView->navigateSearch(direction);
            if (m_searchBar) {
                m_searchBar->setMatchStatus(m_workspaceView->activeSearchMatchIndex(),
                                            m_workspaceView->searchMatchCount());
            }
        }
        return;
    }

    if (m_searchHits.empty()) {
        if (scope == SearchScope::All && m_workspaceView &&
            m_workspaceView->searchMatchCount() > 0) {
            m_workspaceView->navigateSearch(direction);
            if (m_searchBar) {
                m_searchBar->setScopedMatchStatus(m_workspaceView->activeSearchMatchIndex(),
                                                  m_workspaceView->searchMatchCount(), 0,
                                                  m_workspaceView->searchMatchCount());
            }
        }
        return;
    }

    if (direction > 0) {
        m_activeSearchHitIndex = (m_activeSearchHitIndex + 1) % m_searchHits.size();
    } else {
        m_activeSearchHitIndex =
            (m_activeSearchHitIndex + m_searchHits.size() - 1) % m_searchHits.size();
    }

    if (m_searchBar) {
        if (scope == SearchScope::All && m_workspaceView) {
            m_searchBar->setScopedMatchStatus(
                m_activeSearchHitIndex, m_searchHits.size() + m_workspaceView->searchMatchCount(),
                m_searchHits.size(), m_workspaceView->searchMatchCount());
        } else {
            m_searchBar->setMatchStatus(m_activeSearchHitIndex, m_searchHits.size());
        }
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
        m_isAdjustingScrollPosition = true;
        gtk_adjustment_set_value(vadj, std::max(0.0, targetScreenY - pageSize * 0.35));
        m_isAdjustingScrollPosition = false;
    }
}

void DocumentPane::navigateToExcerptSource(std::size_t pageNo, const FluidCore::Rectangle& normRect,
                                           const std::string& excerptId, const std::string& snippet,
                                           const FluidCore::Point& originWorldCoord) {
    if (pageNo >= m_pages.size()) {
        return;
    }

    const auto& page = m_pages[pageNo];
    const double y0 = page.y + normRect.y * page.height;
    const double y1 = y0 + normRect.h * page.height;
    const double passageCenterDocY = (y0 + y1) * 0.5;

    // Squeeze Auto-Expansion: If target passage intersects a compressed/folded region, uncollapse
    // it
    bool needsRebuild = false;
    auto segments = m_squeezeEngine.getSegments(m_docId);
    for (const auto& seg : segments) {
        if (seg.alpha < 0.999 && seg.docYStart < y1 && seg.docYEnd > y0) {
            auto existingFold =
                m_squeezeEngine.findFoldRegionAt(m_docId, passageCenterDocY, (y1 - y0) + 60.0);
            if (existingFold.has_value()) {
                m_squeezeEngine.removeSqueezeRegion(m_docId, existingFold->id);
                needsRebuild = true;
            }
        }
    }

    if (m_highlightViewActive || m_squeezeEngine.isSearchSqueezeActive(m_docId)) {
        // Ensure this excerpt passage is present in active anchors so it remains fully expanded
        bool found = false;
        for (const auto& ea : m_excerptAnchors) {
            if (std::abs(ea.docYStart - y0) < 1.0 && std::abs(ea.docYEnd - y1) < 1.0) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_excerptAnchors.push_back(FluidCore::AnchorSpan{y0, y1, "excerpt", 9});
        }
        if (m_highlightViewActive) {
            applyHighlightSqueeze();
        }
        needsRebuild = true;
    }

    if (needsRebuild) {
        updateLayoutDimensions();
    }

    // Save previous reading scroll position if not already saved
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
    if (vadj && !m_hasSavedReadingState) {
        m_savedReadingScrollY = gtk_adjustment_get_value(vadj);
        m_hasSavedReadingState = true;
    }

    // Center viewport on passage (docYToScreen internally multiplies by m_zoom)
    const double targetScreenY = docYToScreen(passageCenterDocY);
    if (vadj) {
        const double pageSize = gtk_adjustment_get_page_size(vadj);
        m_isAdjustingScrollPosition = true;
        gtk_adjustment_set_value(vadj, std::max(0.0, targetScreenY - pageSize * 0.35));
        m_isAdjustingScrollPosition = false;
    }

    // Pulse highlight animation setup with cancel-and-replace
    if (m_pulseTimerId != 0) {
        g_source_remove(m_pulseTimerId);
        m_pulseTimerId = 0;
    }

    m_pulseHighlight.active = true;
    m_pulseHighlight.pageNo = pageNo;
    m_pulseHighlight.normRect = normRect;
    m_pulseHighlight.alpha = 1.0;
    m_pulseHighlight.startTimeUs = g_get_real_time();

    m_pulseTimerId = g_timeout_add(
        16,
        +[](gpointer data) -> gboolean {
            auto* self = static_cast<DocumentPane*>(data);
            if (!self) {
                return G_SOURCE_REMOVE;
            }

            const gint64 elapsedUs = g_get_real_time() - self->m_pulseHighlight.startTimeUs;
            const double elapsedSec = static_cast<double>(elapsedUs) / 1000000.0;
            const double totalDurationSec = 1.2;

            if (elapsedSec >= totalDurationSec) {
                self->m_pulseHighlight.active = false;
                self->m_pulseHighlight.alpha = 0.0;
                self->m_pulseTimerId = 0;
                if (self->m_area) {
                    gtk_widget_queue_draw(self->m_area);
                }
                return G_SOURCE_REMOVE;
            }

            const double progress = elapsedSec / totalDurationSec;
            self->m_pulseHighlight.alpha = (1.0 - progress) * (1.0 - progress);
            if (self->m_area) {
                gtk_widget_queue_draw(self->m_area);
            }
            return G_SOURCE_CONTINUE;
        },
        this);

    // Present or retarget floating ReturnAnchorPill
    if (m_returnAnchorPill) {
        m_returnAnchorPill->show(excerptId, snippet, originWorldCoord);
    }

    if (m_area) {
        gtk_widget_queue_draw(m_area);
    }
}

bool DocumentPane::hasTextSelection() const {
    return m_inkOverlay ? m_inkOverlay->hasSelection() : false;
}

void DocumentPane::clearTextSelection() {
    if (m_inkOverlay) {
        m_inkOverlay->clearSelection();
        m_inkOverlay->clearCropSelection();
    }
}

void DocumentPane::clearCropSelection() {
    if (m_inkOverlay) {
        m_inkOverlay->clearCropSelection();
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
        return true; // No active PDF document; saving annotations is a no-op success
    }
    return m_annotationStore.saveAnnotations(m_pdfPath);
}

bool DocumentPane::undo() {
    notifyActivated();
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
    notifyActivated();
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
    notifyActivated();
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    const bool shift = (event->state & GDK_SHIFT_MASK) != 0;

    // Zoom Handling (Ctrl + Scroll without Shift)
    if (ctrl && !shift) {
        double delta = 0.0;
        if (event->direction == GDK_SCROLL_UP) {
            delta = -1.0;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            delta = 1.0;
        } else if (event->direction == GDK_SCROLL_SMOOTH) {
            delta = event->delta_y;
        }

        if (delta != 0.0) {
            const double oldZoom = m_zoom;
            const double newZoom = (delta > 0) ? (m_zoom / 1.1) : (m_zoom * 1.1);

            GtkAdjustment* vadj =
                gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
            GtkAdjustment* hadj =
                gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(m_scroller));

            if (vadj && hadj) {
                const double cursorScreenY = event->y;
                const double cursorScreenX = event->x;
                const double oldScrollY = gtk_adjustment_get_value(vadj);
                const double oldScrollX = gtk_adjustment_get_value(hadj);

                setZoom(newZoom);

                const double zoomRatio = m_zoom / oldZoom;
                const double newScrollY = (oldScrollY + cursorScreenY) * zoomRatio - cursorScreenY;
                const double newScrollX = (oldScrollX + cursorScreenX) * zoomRatio - cursorScreenX;

                m_isAdjustingScrollPosition = true;
                gtk_adjustment_set_value(vadj, std::max(0.0, newScrollY));
                gtk_adjustment_set_value(hadj, std::max(0.0, newScrollX));
                m_isAdjustingScrollPosition = false;
            } else {
                setZoom(newZoom);
            }
            return TRUE;
        }
        return FALSE;
    }

    // Dynamic Squeeze Handling (Shift + Scroll or Ctrl + Shift + Scroll)
    if (shift || (ctrl && shift)) {
        double delta = 0.0;
        if (event->direction == GDK_SCROLL_UP) {
            delta = -1.0;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            delta = 1.0;
        } else if (event->direction == GDK_SCROLL_SMOOTH) {
            delta = event->delta_y;
        }

        if (delta != 0.0) {
            applyContinuousSqueezeDelta(delta, event->y);
            return TRUE; // Consume event to prevent default OS horizontal scrolling
        }
        return TRUE;
    }

    return FALSE;
}

void DocumentPane::applyContinuousSqueezeDelta(double delta, double cursorScreenY) {
    const double anchorDocY = screenYToDoc(cursorScreenY);

    // Mode A: Active Search Squeeze
    if (isSearchActive() && !m_searchHits.empty()) {
        if (delta > 0) {
            m_searchSqueezeAlpha = std::clamp(m_searchSqueezeAlpha * 0.85, 0.04, 1.0);
        } else {
            m_searchSqueezeAlpha = std::clamp(m_searchSqueezeAlpha * 1.18, 0.04, 1.0);
        }
        applySearchSqueeze();
        stabilizeViewportAroundCursor(anchorDocY, cursorScreenY);
        return;
    }

    // Mode B: HighlightView Active
    if (m_highlightViewActive) {
        if (delta > 0) {
            m_highlightSqueezeAlpha = std::clamp(m_highlightSqueezeAlpha * 0.85, 0.04, 1.0);
        } else {
            m_highlightSqueezeAlpha = std::clamp(m_highlightSqueezeAlpha * 1.18, 0.04, 1.0);
        }
        applyHighlightSqueeze();
        stabilizeViewportAroundCursor(anchorDocY, cursorScreenY);
        return;
    }

    // Mode C: Plain Squeeze (Downward Fold-and-Pull from Cursor)
    if (!m_hasActiveFoldGesture) {
        m_hasActiveFoldGesture = true;

        // Check if cursor is near an existing fold to continue adjusting it
        auto existingFold = m_squeezeEngine.findFoldRegionAt(m_docId, anchorDocY, 80.0);
        if (existingFold.has_value()) {
            m_activeFoldRegionId = existingFold->id;
            m_activeFoldStartDocY = existingFold->yStart;
            m_activeFoldSpan = existingFold->yEnd - existingFold->yStart;
            // Crucial: remove existing fold from rawRegions so the live preview cleanly drives it
            m_squeezeEngine.removeSqueezeRegion(m_docId, m_activeFoldRegionId);
        } else {
            m_activeFoldRegionId.clear();
            m_activeFoldStartDocY = anchorDocY;
            m_activeFoldSpan = 0.0;
        }
    }

    if (delta > 0) {
        // Scrolling DOWN: expand the fold downward (pull content from below into the crease)
        const double step = 90.0 * std::max(1.0, std::abs(delta));
        m_activeFoldSpan += step;
    } else {
        // Scrolling UP: shrink the fold (unfold the content back out)
        const double step = 90.0 * std::max(1.0, std::abs(delta));
        m_activeFoldSpan = std::max(0.0, m_activeFoldSpan - step);
    }

    if (!m_pages.empty()) {
        const double maxDocH = m_pages.back().y + m_pages.back().height;
        if (m_activeFoldStartDocY + m_activeFoldSpan > maxDocH) {
            m_activeFoldSpan = maxDocH - m_activeFoldStartDocY;
        }
    }

    const double y0 = m_activeFoldStartDocY;
    const double y1 = m_activeFoldStartDocY + m_activeFoldSpan;

    if (m_activeFoldSpan <= 15.0) {
        // Fold is too small or reverse-scrolled to 0: clear it
        m_squeezeEngine.clearPreviewSqueezeRegion(m_docId);
        m_activeFoldRegionId.clear();
    } else {
        // Squeeze [y0, y1] with alpha = 0.04 (96% collapsed crease)
        m_squeezeEngine.setPreviewSqueezeRegion(m_docId, y0, y1, 0.04);
    }
    updateLayoutDimensions();
    stabilizeViewportAroundCursor(anchorDocY, cursorScreenY);

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
}

void DocumentPane::stabilizeViewportAroundCursor(double anchorDocY, double cursorScreenY) {
    if (m_isAdjustingScrollPosition) {
        return;
    }

    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller));
    if (!vadj) {
        return;
    }

    const double newScreenY = docYToScreen(anchorDocY);
    const double deltaY = newScreenY - cursorScreenY;
    const double currentScrollY = gtk_adjustment_get_value(vadj);

    m_isAdjustingScrollPosition = true;
    gtk_adjustment_set_value(vadj, std::max(0.0, currentScrollY + deltaY));
    m_isAdjustingScrollPosition = false;
}

void DocumentPane::commitScrollSqueeze() {
    if (!m_hasActiveFoldGesture) {
        return;
    }

    const double y0 = m_activeFoldStartDocY;
    const double y1 = m_activeFoldStartDocY + m_activeFoldSpan;

    m_hasActiveFoldGesture = false;
    m_squeezeEngine.clearPreviewSqueezeRegion(m_docId);

    if (m_activeFoldSpan > 15.0) {
        setSqueezeFold(y0, y1, 0.04);
    } else {
        updateLayoutDimensions();
    }
    m_activeFoldRegionId.clear();
    m_activeFoldSpan = 0.0;
}

void DocumentPane::draw(cairo_t* cr) {
    if (m_pages.empty() || !m_document || !m_squeezeEngine.hasDocument(m_docId)) {
        cairo_set_source_rgb(cr, 0.906, 0.906, 0.894);
        cairo_paint(cr);
        return;
    }

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

    // Viewport-aware draw clipping: query ScrolledWindow vertical adjustment
    // to strictly limit rendering to the active on-screen viewport and prevent
    // catastrophic out-of-memory crashes on multi-hundred-page documents.
    GtkAdjustment* vadj =
        m_scroller ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(m_scroller)) : nullptr;
    const double viewportY = vadj ? gtk_adjustment_get_value(vadj) : 0.0;
    const double viewportH =
        vadj ? gtk_adjustment_get_page_size(vadj) : static_cast<double>(allocation.height);

    // Pre-fetch bounds: viewport plus 400px margin for smooth scrolling
    const double viewYStart = std::max(0.0, viewportY - 400.0) / m_zoom;
    const double viewYEnd = (viewportY + viewportH + 400.0) / m_zoom;

    const double clipYStart = std::max(viewYStart, clip.y / m_zoom);
    const double clipYEnd = std::min(viewYEnd, (clip.y + clip.height) / m_zoom);

    if (clipYStart >= clipYEnd) {
        return;
    }

    cairo_scale(cr, m_zoom, m_zoom);

    const auto segments = m_squeezeEngine.getSegments(m_docId);
    const double pageX =
        kPageMargin + std::max(0.0, (allocation.width / m_zoom - m_layoutWidth) / 2.0);

    std::vector<std::size_t> visiblePageIndices;
    if (m_pages.empty()) {
        return;
    }

    // Map screen clip bounds to document coordinates for accurate vertical culling
    const double docY0 = m_squeezeEngine.mapScreenYToDocument(clipYStart, m_docId).screenY;
    const double docY1 = m_squeezeEngine.mapScreenYToDocument(clipYEnd, m_docId).screenY;
    const double cDocMin = std::min(docY0, docY1);
    const double cDocMax = std::max(docY0, docY1);

    // Binary search to find the first page near the visible viewport
    auto startIt = std::lower_bound(
        m_pages.begin(), m_pages.end(), cDocMin - 1000.0,
        [](const PageLayout& page, double val) { return (page.y + page.height) < val; });
    std::size_t startIdx = std::distance(m_pages.begin(), startIt);

    for (std::size_t i = startIdx; i < m_pages.size(); ++i) {
        const PageLayout& layout = m_pages[i];
        if (layout.y > cDocMax + 1000.0) {
            break; // Pages are sorted vertically, stop early
        }
        if (layout.y + layout.height < cDocMin - 1000.0) {
            continue; // Skip pages far above the active viewport
        }

        auto slices = SqueezeRenderHelper::decomposePage(i, layout.y, layout.height, segments);

        bool pageVisible = false;
        for (const auto& slice : slices) {
            if (slice.screenYEnd < clipYStart || slice.screenYStart > clipYEnd) {
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
        if (!surface && layout.page && !m_isZooming) {
            // Render high-DPI surface scaled by m_zoom
            surface = m_pageTileCache.renderPage(i, layout.page, layout.width * m_zoom,
                                                 layout.height * m_zoom);
        }

        for (const auto& slice : slices) {
            if (slice.screenYEnd < clipYStart || slice.screenYStart > clipYEnd) {
                continue;
            }

            cairo_save(cr);
            cairo_rectangle(cr, pageX, slice.screenYStart, layout.width,
                            slice.screenYEnd - slice.screenYStart);
            cairo_clip(cr);

            if (surface) {
                const double yOffset = slice.screenYStart - slice.pageLocalDocYStart;
                cairo_save(cr);
                cairo_translate(cr, pageX, yOffset);
                cairo_scale(cr, 1.0 / m_zoom, 1.0 / m_zoom);
                cairo_set_source_surface(cr, surface.get(), 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
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

                    cairo_rectangle(cr, pageX + rX0 - 2.0, yOffset + rY0 - 1.0, (rX1 - rX0) + 4.0,
                                    (rY1 - rY0) + 2.0);
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

            // Render luminous pulse highlight for navigated excerpt source
            if (m_pulseHighlight.active && m_pulseHighlight.pageNo == i &&
                m_pulseHighlight.alpha > 0.01) {
                const double yOffset = slice.screenYStart - slice.pageLocalDocYStart;
                const double rY0 = m_pulseHighlight.normRect.y * layout.height;
                const double rY1 = rY0 + m_pulseHighlight.normRect.h * layout.height;
                const double rX0 = m_pulseHighlight.normRect.x * layout.width;
                const double rX1 = rX0 + m_pulseHighlight.normRect.w * layout.width;

                if (rY1 >= slice.pageLocalDocYStart && rY0 <= slice.pageLocalDocYEnd) {
                    const double pulseAlpha = m_pulseHighlight.alpha;
                    cairo_save(cr);

                    // Outer glowing halo
                    cairo_set_source_rgba(cr, 0.05, 0.60, 1.0, 0.30 * pulseAlpha);
                    drawRoundedRect(cr, pageX + rX0 - 4.0, yOffset + rY0 - 3.0, (rX1 - rX0) + 8.0,
                                    (rY1 - rY0) + 6.0, 6.0);
                    cairo_fill(cr);

                    // Radiant translucent fill
                    cairo_set_source_rgba(cr, 0.20, 0.70, 1.0, 0.25 * pulseAlpha);
                    drawRoundedRect(cr, pageX + rX0, yOffset + rY0, (rX1 - rX0), (rY1 - rY0), 4.0);
                    cairo_fill_preserve(cr);

                    // Neon cyan stroke border
                    cairo_set_source_rgba(cr, 0.0, 0.65, 1.0, 0.95 * pulseAlpha);
                    cairo_set_line_width(cr, 2.0);
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
    if (!m_squeezeEngine.isSearchSqueezeActive(m_docId) &&
        !m_squeezeEngine.isHighlightSqueezeActive(m_docId)) {
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
