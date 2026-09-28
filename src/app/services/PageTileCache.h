#pragma once

#include "services/MemoryBudget.h"

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    // Default budget is now a slice of one global pool rather than an independent 64 MB
    // ceiling. Callers that pass an explicit maxBytes (tests, the scalability benchmark)
    // are unaffected.
    static std::size_t defaultMaxBytes() {
        return MemoryBudget::instance().sliceBytes(MemoryBudget::Slice::PageTiles);
    }
    static constexpr std::size_t kDefaultMaxBytes = 64 * 1024 * 1024; // legacy per-cache ceiling
    static constexpr std::size_t kDefaultMaxPages = 8;

    // Per-surface dimension ceiling (Fix 5D). DocumentPane's zoom is not itself clamped,
    // so a single letter page at 10x would otherwise ask for a 6120x7920 ARGB surface -
    // ~194 MB for one page. Clamping the raster bounds the damage to one page instead of
    // the process, at the cost of a soft-rendered page beyond this zoom.
    static constexpr int kMaxSurfaceDimension = 4096;

    explicit PageTileCache(std::size_t maxBytes = static_cast<std::size_t>(-1),
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

    // Ejects least-recently-used surfaces until currentBytes() <= targetBytes, skipping
    // pinned (currently visible) pages. Used by the idle trim, which has to release
    // surfaces *before* _heapmin() can hand anything back to the OS.
    std::size_t trimToBytes(std::size_t targetBytes);

    // Releases every unpinned surface. Returns the bytes reclaimed.
    std::size_t releaseUnpinned();

    std::size_t currentBytes() const { return m_currentBytes; }
    std::size_t maxBytes() const { return m_maxBytes; }
    void setMaxBytes(std::size_t maxBytes) { m_maxBytes = maxBytes; }

    std::size_t size() const { return m_lruList.size(); }
    std::size_t maxPages() const { return m_maxPages; }
    void setMaxPages(std::size_t maxPages) { m_maxPages = maxPages; }

    static void setNullSinkMode(bool enable) { s_nullSinkMode = enable; }
    static bool isNullSinkMode() { return s_nullSinkMode; }

    struct TileSurfaceInfo {
        std::size_t pageIndex = 0;
        int width = 0;
        int height = 0;
        std::size_t bytes = 0;
        bool pinned = false;
    };

    struct PageTileCacheStats {
        std::size_t entryCount = 0;
        std::size_t currentBytes = 0;
        std::size_t maxBytes = 0;
        std::size_t maxPages = 0;
        std::vector<std::size_t> residentPages;
        std::vector<std::size_t> pinnedPages;
        std::vector<TileSurfaceInfo> surfaces;
    };

    PageTileCacheStats getStats() const;
    void dumpStats(const std::string& tag) const;
    const std::unordered_set<std::size_t>& everRenderedPages() const { return m_everRenderedPages; }

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

    // Largest single surface admitted so far. Pinning holds back this much headroom so
    // that one more incoming page always fits, which is what keeps eviction from being
    // blocked at the exact moment it is needed (Fix 5D).
    std::size_t m_largestSurfaceBytes = 0;

    std::list<CacheNode> m_lruList;
    std::unordered_map<std::size_t, std::list<CacheNode>::iterator> m_lookup;
    std::unordered_set<std::size_t> m_everRenderedPages;
    inline static bool s_nullSinkMode = false;
};

} // namespace FluidCoreApp
