#pragma once

#include "PageTileCache.h"
#include "PdfDocumentService.h"
#include "workspace/ExcerptCardNode.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cairo.h>
#include <glib.h>

namespace FluidCoreApp {

// Discrete Level-of-Detail (LoD) zoom tiers relative to base PDF points:
// 1.0x corresponds to 1 pixel per PDF point (72 DPI).
enum class LodTier : int {
    Overview = 0, // 0.5x (~36 DPI)
    Standard = 1, // 1.0x (~72 DPI)
    HiDpi = 2,    // 2.0x (~144 DPI)
    Retina = 3,   // 4.0x (~288 DPI)
    Ultra = 4     // 8.0x (~576 DPI, clamped to max 1536px)
};

LodTier computeLodTierFromZoom(double canvasZoom);
double getLodTierScale(LodTier tier);

// Quantized cache key for visual diagram crop tiles
struct CropCacheKey {
    std::string docId;
    std::size_t pageNo = 0;
    uint16_t xNorm = 0;
    uint16_t yNorm = 0;
    uint16_t wNorm = 0;
    uint16_t hNorm = 0;
    LodTier tier = LodTier::Standard;

    static CropCacheKey fromNormalizedRect(const std::string& docId, std::size_t pageNo,
                                           const FluidCore::Rectangle& normRect, LodTier tier);

    bool operator==(const CropCacheKey& other) const {
        return docId == other.docId && pageNo == other.pageNo && xNorm == other.xNorm &&
               yNorm == other.yNorm && wNorm == other.wNorm && hNorm == other.hNorm &&
               tier == other.tier;
    }
};

struct CropCacheKeyHash {
    std::size_t operator()(const CropCacheKey& k) const {
        std::size_t h1 = std::hash<std::string>{}(k.docId);
        std::size_t h2 = std::hash<std::size_t>{}(k.pageNo);
        std::size_t h3 = (static_cast<std::size_t>(k.xNorm) << 16) | k.yNorm;
        std::size_t h4 = (static_cast<std::size_t>(k.wNorm) << 16) | k.hNorm;
        std::size_t h5 = static_cast<std::size_t>(k.tier);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
    }
};

// Byte-bounded LRU visual diagram crop tile cache for ExcerptCardNodes.
// Enforces 128 MB default memory limit, clamps max tile dimensions to 1536px,
// and supports both synchronous rasterization and asynchronous worker pool rendering.
class ExcerptTileCache {
  public:
    static constexpr std::size_t kDefaultMaxBytes = 128 * 1024 * 1024; // 128 MB
    static constexpr int kMaxTileDimension = 1536;                     // 1536 px clamp
    static constexpr int kMinTileDimension = 16;                       // 16 px minimum

    using RenderReadyCallback =
        std::function<void(const std::string& excerptId, uint64_t requestId)>;

    explicit ExcerptTileCache(PdfDocumentService& docService,
                              std::size_t maxBytes = kDefaultMaxBytes);
    ~ExcerptTileCache();

    ExcerptTileCache(const ExcerptTileCache&) = delete;
    ExcerptTileCache& operator=(const ExcerptTileCache&) = delete;

    void setRenderReadyCallback(RenderReadyCallback cb) { m_onRenderReady = std::move(cb); }

    // Retrieves cached surface for the requested key, promoting it to MRU.
    CairoSurfaceHandle get(const CropCacheKey& key);

    // Finds the best available existing cached surface for the same crop across any LoD tier.
    CairoSurfaceHandle getBestAvailableSurface(const std::string& docId, std::size_t pageNo,
                                               const FluidCore::Rectangle& normRect);

    // Dispatches an asynchronous render task to background GThreadPool if not cached.
    // Returns immediate cached surface or empty handle, and returns requestId.
    uint64_t requestCropAsync(const std::string& excerptId, const std::string& docId,
                              std::size_t pageNo, const FluidCore::Rectangle& normRect,
                              double cardWidthPt, double cardHeightPt, double canvasZoom);

    // Synchronous crop rasterization (used for unit tests and immediate startup)
    CairoSurfaceHandle renderCropSync(const std::string& docId, std::size_t pageNo,
                                      const FluidCore::Rectangle& normRect, double targetWidthPx,
                                      double targetHeightPx, PopplerPage* page = nullptr);

    // Inserts a pre-rendered surface directly into the LRU cache
    void insert(const CropCacheKey& key, CairoSurfaceHandle handle);

    void cancelRequest(uint64_t requestId);
    void cancelDocumentRequests(const std::string& docId);
    void invalidate(const std::string& docId);
    void clear();

    std::size_t currentBytes() const { return m_currentBytes; }
    std::size_t maxBytes() const { return m_maxBytes; }
    void setMaxBytes(std::size_t maxBytes) { m_maxBytes = maxBytes; }

    std::size_t size() const { return m_lruList.size(); }

  private:
    struct CacheNode {
        CropCacheKey key;
        CairoSurfaceHandle surface;
        std::size_t bytes = 0;
    };

    struct AsyncRenderTask {
        uint64_t requestId = 0;
        std::string excerptId;
        std::string docId;
        std::size_t pageNo = 0;
        FluidCore::Rectangle normRect{0.0, 0.0, 1.0, 1.0};
        CropCacheKey cacheKey;
        int targetPixelW = 0;
        int targetPixelH = 0;
        ExcerptTileCache* cache = nullptr;
    };

    void evict(std::size_t incomingBytes);
    static void asyncWorkerFunc(gpointer data, gpointer userData);
    static gboolean onRenderCompletedIdle(gpointer data);

    PdfDocumentService& m_docService;
    std::size_t m_maxBytes = kDefaultMaxBytes;
    std::size_t m_currentBytes = 0;

    std::list<CacheNode> m_lruList;
    std::unordered_map<CropCacheKey, std::list<CacheNode>::iterator, CropCacheKeyHash> m_lookup;

    GThreadPool* m_threadPool = nullptr;
    std::atomic<uint64_t> m_nextRequestId{1};
    std::unordered_set<uint64_t> m_activeRequestIds;
    std::unordered_set<uint64_t> m_cancelledRequestIds;

    RenderReadyCallback m_onRenderReady;
};

} // namespace FluidCoreApp
