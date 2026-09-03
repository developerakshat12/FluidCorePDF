#include "PageTileCache.h"
#include "services/PdfDocumentService.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace FluidCoreApp {

PageTileCache::PageTileCache(std::size_t maxBytes, std::size_t maxPages)
    : m_maxBytes(maxBytes), m_maxPages(maxPages) {}

PageTileCache::~PageTileCache() {
    clear();
}

PageTileCache::PageTileCache(PageTileCache&& other) noexcept
    : m_maxBytes(other.m_maxBytes), m_maxPages(other.m_maxPages),
      m_currentBytes(other.m_currentBytes), m_lruList(std::move(other.m_lruList)),
      m_lookup(std::move(other.m_lookup)) {
    other.m_currentBytes = 0;
}

PageTileCache& PageTileCache::operator=(PageTileCache&& other) noexcept {
    if (this != &other) {
        clear();
        m_maxBytes = other.m_maxBytes;
        m_maxPages = other.m_maxPages;
        m_currentBytes = other.m_currentBytes;
        m_lruList = std::move(other.m_lruList);
        m_lookup = std::move(other.m_lookup);
        other.m_currentBytes = 0;
    }
    return *this;
}

CairoSurfaceHandle PageTileCache::get(std::size_t pageIndex) {
    auto it = m_lookup.find(pageIndex);
    if (it == m_lookup.end()) {
        return CairoSurfaceHandle{};
    }

    // Promote to Most Recently Used (front of list)
    m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
    return it->second->surface;
}

void PageTileCache::evict(std::size_t incomingBytes) {
    while ((m_currentBytes + incomingBytes > m_maxBytes || m_lruList.size() >= m_maxPages) &&
           !m_lruList.empty()) {
        // Find least recently used unpinned page from back of list
        auto it = m_lruList.end();
        bool foundUnpinned = false;
        while (it != m_lruList.begin()) {
            --it;
            if (!it->pinned) {
                foundUnpinned = true;
                break;
            }
        }

        if (!foundUnpinned) {
            // All resident pages are pinned: prevent evicting active viewport content
            break;
        }

        m_lookup.erase(it->pageIndex);
        m_currentBytes -= it->bytes;
        m_lruList.erase(it);
    }
}

void PageTileCache::insert(std::size_t pageIndex, CairoSurfaceHandle handle) {
    if (!handle) {
        return;
    }

    const std::size_t bytes = handle.byteSize();
    auto it = m_lookup.find(pageIndex);
    if (it != m_lookup.end()) {
        m_currentBytes -= it->second->bytes;
        m_currentBytes += bytes;
        it->second->surface = handle;
        it->second->bytes = bytes;
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
    } else {
        evict(bytes);
        m_lruList.push_front(CacheNode{pageIndex, handle, bytes, false});
        m_lookup[pageIndex] = m_lruList.begin();
        m_currentBytes += bytes;
    }
}

CairoSurfaceHandle PageTileCache::renderPage(std::size_t pageIndex, PopplerPage* page,
                                             double targetWidth, double targetHeight) {
    if (!page) {
        return CairoSurfaceHandle{};
    }

    const int width = std::max(1, static_cast<int>(std::round(targetWidth)));
    const int height = std::max(1, static_cast<int>(std::round(targetHeight)));

    double pageWidth = 0.0;
    double pageHeight = 0.0;
    poppler_page_get_size(page, &pageWidth, &pageHeight);
    if (pageWidth <= 0.0)
        pageWidth = 1.0;
    if (pageHeight <= 0.0)
        pageHeight = 1.0;

    cairo_surface_t* rawSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(rawSurface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(rawSurface);
        return CairoSurfaceHandle{};
    }

    cairo_t* cr = cairo_create(rawSurface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    const double scaleX = static_cast<double>(width) / pageWidth;
    const double scaleY = static_cast<double>(height) / pageHeight;
    cairo_scale(cr, scaleX, scaleY);

    {
        std::lock_guard<std::mutex> popplerLock(PdfDocumentService::globalPopplerMutex());
        poppler_page_render(page, cr);
    }
    cairo_destroy(cr);

    CairoSurfaceHandle handle(rawSurface, /*takeOwnership=*/true);
    insert(pageIndex, handle);
    return handle;
}

void PageTileCache::setPinnedPages(const std::vector<std::size_t>& pages) {
    unpinAll();
    std::size_t pinnedCount = 0;
    for (std::size_t p : pages) {
        if (pinnedCount >= m_maxPages) {
            break;
        }
        auto it = m_lookup.find(p);
        if (it != m_lookup.end()) {
            it->second->pinned = true;
            ++pinnedCount;
        }
    }
}

void PageTileCache::unpinAll() {
    for (auto& node : m_lruList) {
        node.pinned = false;
    }
}

void PageTileCache::invalidate(std::size_t pageIndex) {
    auto it = m_lookup.find(pageIndex);
    if (it != m_lookup.end()) {
        m_currentBytes -= it->second->bytes;
        m_lruList.erase(it->second);
        m_lookup.erase(it);
    }
}

void PageTileCache::clear() {
    m_lookup.clear();
    m_lruList.clear();
    m_currentBytes = 0;
}

} // namespace FluidCoreApp
