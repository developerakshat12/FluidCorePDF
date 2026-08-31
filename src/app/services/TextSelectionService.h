#pragma once

#include "document/DocumentPane.h"
#include "text/TextSelection.h"

#include <poppler.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace FluidCoreApp {

class TextSelectionService {
  public:
    TextSelectionService() = default;
    ~TextSelectionService() = default;

    TextSelectionService(const TextSelectionService&) = delete;
    TextSelectionService& operator=(const TextSelectionService&) = delete;

    // Cache glyph layout bounding boxes for a page if not already cached.
    void cacheGlyphLayout(std::size_t pageIdx, PopplerPage* page);

    // Update active multi-page selection during live pointer drag.
    void updateLiveDrag(const std::vector<DocumentPane::PageLayout>& pages, std::size_t startPage,
                        const FluidCore::SelectionPoint& startPt, std::size_t currPage,
                        const FluidCore::SelectionPoint& currPt,
                        FluidCore::MultiPageSelectionState& state);

    // Finalize text extraction for all selected pages using poppler_page_get_selected_text.
    void finalizeSelection(const std::vector<DocumentPane::PageLayout>& pages,
                           FluidCore::MultiPageSelectionState& state);

    // Dispatch text string to GTK selection clipboard (and primary selection on Linux).
    static bool copyToClipboard(const std::string& text);

    // Clear cached glyph layouts (e.g. on document close/reload).
    void clearCache() { m_pageGlyphCache.clear(); }

  private:
    std::unordered_map<std::size_t, std::vector<FluidCore::SelectionRect>> m_pageGlyphCache;
};

} // namespace FluidCoreApp
