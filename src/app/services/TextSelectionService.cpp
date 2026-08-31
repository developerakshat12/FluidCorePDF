#include "TextSelectionService.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace FluidCoreApp {

void TextSelectionService::cacheGlyphLayout(std::size_t pageIdx, PopplerPage* page) {
    if (!page || m_pageGlyphCache.find(pageIdx) != m_pageGlyphCache.end()) {
        return;
    }

    PopplerRectangle* rectArray = nullptr;
    guint nRects = 0;
    if (poppler_page_get_text_layout(page, &rectArray, &nRects) && rectArray) {
        std::vector<FluidCore::SelectionRect> glyphs;
        glyphs.reserve(nRects);
        for (guint i = 0; i < nRects; ++i) {
            glyphs.push_back(FluidCore::TextSelection::normalize(rectArray[i].x1, rectArray[i].y1,
                                                                 rectArray[i].x2, rectArray[i].y2));
        }
        g_free(rectArray);
        m_pageGlyphCache[pageIdx] = std::move(glyphs);
    } else {
        m_pageGlyphCache[pageIdx] = {};
    }
}

void TextSelectionService::updateLiveDrag(const std::vector<DocumentPane::PageLayout>& pages,
                                          std::size_t startPage,
                                          const FluidCore::SelectionPoint& startPt,
                                          std::size_t currPage,
                                          const FluidCore::SelectionPoint& currPt,
                                          FluidCore::MultiPageSelectionState& state) {
    if (pages.empty() || startPage >= pages.size() || currPage >= pages.size()) {
        return;
    }

    state.hasSelection = true;
    state.startPoint = startPt;
    state.endPoint = currPt;

    const std::size_t pMin = std::min(startPage, currPage);
    const std::size_t pMax = std::max(startPage, currPage);
    state.startPage = pMin;
    state.endPage = pMax;

    state.pages.clear();
    state.pages.reserve(pMax - pMin + 1);

    for (std::size_t p = pMin; p <= pMax; ++p) {
        if (p >= pages.size() || !pages[p].page) {
            continue;
        }

        cacheGlyphLayout(p, pages[p].page);
        const auto& glyphs = m_pageGlyphCache[p];
        const double pWidth = pages[p].width;
        const double pHeight = pages[p].height;

        FluidCore::SelectionRect dragBox;
        if (pMin == pMax) {
            // Single page selection
            dragBox = FluidCore::TextSelection::normalize(startPt.x, startPt.y, currPt.x, currPt.y);
        } else if (startPage < currPage) {
            // Downward drag across pages
            if (p == startPage) {
                dragBox = FluidCore::SelectionRect{startPt.x, startPt.y, pWidth, pHeight};
            } else if (p == currPage) {
                dragBox = FluidCore::SelectionRect{0.0, 0.0, currPt.x, currPt.y};
            } else {
                dragBox = FluidCore::SelectionRect{0.0, 0.0, pWidth, pHeight};
            }
        } else {
            // Upward drag across pages
            if (p == currPage) {
                dragBox = FluidCore::SelectionRect{currPt.x, currPt.y, pWidth, pHeight};
            } else if (p == startPage) {
                dragBox = FluidCore::SelectionRect{0.0, 0.0, startPt.x, startPt.y};
            } else {
                dragBox = FluidCore::SelectionRect{0.0, 0.0, pWidth, pHeight};
            }
        }

        std::vector<FluidCore::SelectionRect> intersecting;
        for (const auto& g : glyphs) {
            if (FluidCore::TextSelection::intersects(g, dragBox)) {
                intersecting.push_back(g);
            }
        }

        auto lineRects = FluidCore::TextSelection::coalesceLineRects(intersecting);
        state.pages.push_back(FluidCore::PageTextSelection{p, dragBox, std::move(lineRects), ""});
    }
}

void TextSelectionService::finalizeSelection(const std::vector<DocumentPane::PageLayout>& pages,
                                             FluidCore::MultiPageSelectionState& state) {
    if (state.pages.empty() || pages.empty()) {
        return;
    }

    for (auto& pageSel : state.pages) {
        if (pageSel.pageIndex >= pages.size() || !pages[pageSel.pageIndex].page) {
            continue;
        }

        PopplerPage* page = pages[pageSel.pageIndex].page;
        PopplerRectangle pRect{pageSel.dragBounds.x0, pageSel.dragBounds.y0, pageSel.dragBounds.x1,
                               pageSel.dragBounds.y1};

        char* text = poppler_page_get_selected_text(page, POPPLER_SELECTION_GLYPH, &pRect);
        if (text) {
            pageSel.text = std::string(text);
            g_free(text);
        } else {
            // Fallback to text_for_area if get_selected_text returned null
            char* areaText = poppler_page_get_text_for_area(page, &pRect);
            if (areaText) {
                pageSel.text = std::string(areaText);
                g_free(areaText);
            }
        }
    }

    state.fullText = FluidCore::TextSelection::formatClipboardText(state.pages);
}

bool TextSelectionService::copyToClipboard(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clipboard) {
        gtk_clipboard_set_text(clipboard, text.c_str(), static_cast<gint>(text.size()));
    }

#ifndef G_OS_WIN32
    GtkClipboard* primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (primary) {
        gtk_clipboard_set_text(primary, text.c_str(), static_cast<gint>(text.size()));
    }
#endif

    return true;
}

} // namespace FluidCoreApp
