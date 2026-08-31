#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

#include <cairo.h>
#include <poppler.h>

namespace FluidCoreApp {

// RAII reference-counted handle wrapping cairo_surface_t*.
// Ensures surfaces remain valid during active compositing passes even if
// evicted from the underlying PageTileCache LRU list.
class CairoSurfaceHandle {
  public:
    CairoSurfaceHandle() : m_surface(nullptr) {}

    explicit CairoSurfaceHandle(cairo_surface_t* surface, bool takeOwnership = false)
        : m_surface(surface) {
        if (m_surface && !takeOwnership) {
            cairo_surface_reference(m_surface);
        }
    }

    ~CairoSurfaceHandle() {
        if (m_surface) {
            cairo_surface_destroy(m_surface);
        }
    }

    CairoSurfaceHandle(const CairoSurfaceHandle& other) : m_surface(other.m_surface) {
        if (m_surface) {
            cairo_surface_reference(m_surface);
        }
    }

    CairoSurfaceHandle& operator=(const CairoSurfaceHandle& other) {
        if (this != &other) {
            if (m_surface) {
                cairo_surface_destroy(m_surface);
            }
            m_surface = other.m_surface;
            if (m_surface) {
                cairo_surface_reference(m_surface);
            }
        }
        return *this;
    }

    CairoSurfaceHandle(CairoSurfaceHandle&& other) noexcept : m_surface(other.m_surface) {
        other.m_surface = nullptr;
    }

    CairoSurfaceHandle& operator=(CairoSurfaceHandle&& other) noexcept {
        if (this != &other) {
            if (m_surface) {
                cairo_surface_destroy(m_surface);
            }
            m_surface = other.m_surface;
            other.m_surface = nullptr;
        }
        return *this;
    }

    cairo_surface_t* get() const { return m_surface; }
    explicit operator bool() const { return m_surface != nullptr; }

    int width() const { return m_surface ? cairo_image_surface_get_width(m_surface) : 0; }

    int height() const { return m_surface ? cairo_image_surface_get_height(m_surface) : 0; }

    std::size_t byteSize() const {
        return static_cast<std::size_t>(width()) * static_cast<std::size_t>(height()) * 4;
    }

  private:
    cairo_surface_t* m_surface = nullptr;
};

// Byte-bounded LRU page tile cache for DocumentPane.
// Avoids repeated poppler_page_render calls during continuous scrolling,
// protects actively visible pages from eviction (anti-thrashing), and enforces
// a strict memory working set fraction (default 64 MB <= 1.2 GB limit).
class PageTileCache {
  public:
    static constexpr std::size_t kDefaultMaxBytes = 64 * 1024 * 1024; // 64 MB
    static constexpr std::size_t kDefaultMaxPages = 8;

    explicit PageTileCache(std::size_t maxBytes = kDefaultMaxBytes,
                           std::size_t maxPages = kDefaultMaxPages);
    ~PageTileCache();

    PageTileCache(const PageTileCache&) = delete;
    PageTileCache& operator=(const PageTileCache&) = delete;

    PageTileCache(PageTileCache&& other) noexcept;
    PageTileCache& operator=(PageTileCache&& other) noexcept;

    // Retrieves cached surface and promotes it to MRU. Returns empty handle if not cached.
    CairoSurfaceHandle get(std::size_t pageIndex);

    // Renders PopplerPage to Cairo image surface, caches it, evicts LRU if needed,
    // and returns a refcounted handle.
    CairoSurfaceHandle renderPage(std::size_t pageIndex, PopplerPage* page, double targetWidth,
                                  double targetHeight);

    // Inserts a pre-rendered surface handle directly into the cache.
    void insert(std::size_t pageIndex, CairoSurfaceHandle handle);

    // Marks active visible pages as pinned so they will not be evicted during scrolling bursts.
    void setPinnedPages(const std::vector<std::size_t>& pages);
    void unpinAll();

    void invalidate(std::size_t pageIndex);
    void clear();

    std::size_t currentBytes() const { return m_currentBytes; }
    std::size_t maxBytes() const { return m_maxBytes; }
    void setMaxBytes(std::size_t maxBytes) { m_maxBytes = maxBytes; }

    std::size_t size() const { return m_lruList.size(); }
    std::size_t maxPages() const { return m_maxPages; }
    void setMaxPages(std::size_t maxPages) { m_maxPages = maxPages; }

  private:
    struct CacheNode {
        std::size_t pageIndex = 0;
        CairoSurfaceHandle surface;
        std::size_t bytes = 0;
        bool pinned = false;
    };

    void evict(std::size_t incomingBytes);

    std::size_t m_maxBytes;
    std::size_t m_maxPages;
    std::size_t m_currentBytes = 0;

    std::list<CacheNode> m_lruList;
    std::unordered_map<std::size_t, std::list<CacheNode>::iterator> m_lookup;
};

} // namespace FluidCoreApp
