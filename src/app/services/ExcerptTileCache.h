#pragma once

#include "services/MemoryBudget.h"
#include "services/PageTileCache.h"
#include "services/PdfDocumentService.h"
#include "services/TileSizing.h"
#include "storage/AnnotationStore.h"
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

// Unique crop region identity across all LoD zoom tiers (for viewport pinning)
struct CropIdentity {
    std::string docId;
    std::size_t pageNo = 0;
    uint16_t xNorm = 0;
    uint16_t yNorm = 0;
    uint16_t wNorm = 0;
    uint16_t hNorm = 0;

    static CropIdentity fromNormalizedRect(const std::string& docId, std::size_t pageNo,
                                           const FluidCore::Rectangle& normRect) {
        CropIdentity id;
        id.docId = docId;
        id.pageNo = pageNo;
        id.xNorm = static_cast<uint16_t>(std::clamp(normRect.x, 0.0, 1.0) * 65535.0 + 0.5);
        id.yNorm = static_cast<uint16_t>(std::clamp(normRect.y, 0.0, 1.0) * 65535.0 + 0.5);
        id.wNorm = static_cast<uint16_t>(std::clamp(normRect.w, 0.0, 1.0) * 65535.0 + 0.5);
        id.hNorm = static_cast<uint16_t>(std::clamp(normRect.h, 0.0, 1.0) * 65535.0 + 0.5);
        return id;
    }

    bool operator==(const CropIdentity& o) const {
        return docId == o.docId && pageNo == o.pageNo && xNorm == o.xNorm && yNorm == o.yNorm &&
               wNorm == o.wNorm && hNorm == o.hNorm;
    }
};

struct CropIdentityHash {
    std::size_t operator()(const CropIdentity& k) const {
        std::size_t h1 = std::hash<std::string>{}(k.docId);
        std::size_t h2 = std::hash<std::size_t>{}(k.pageNo);
        std::size_t h3 = (static_cast<std::size_t>(k.xNorm) << 16) | k.yNorm;
        std::size_t h4 = (static_cast<std::size_t>(k.wNorm) << 16) | k.hNorm;
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

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

    // True when both keys address the same region of the same page, regardless of
    // resolution. Inserting a higher tier supersedes every lower one, so this is what
    // identifies the tiles that must be dropped alongside the incoming one.
    bool sameCropAs(const CropCacheKey& other) const {
        return docId == other.docId && pageNo == other.pageNo && xNorm == other.xNorm &&
               yNorm == other.yNorm && wNorm == other.wNorm && hNorm == other.hNorm;
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
// Enforces 64 MB default memory limit, clamps max tile dimensions to 1536px,
// and supports both synchronous rasterization and asynchronous worker pool rendering.
class ExcerptTileCache {
  public:
    // Default budget is a slice of one global pool rather than an independent 64 MB
    // ceiling. Callers that pass an explicit maxBytes are unaffected.
    static std::size_t defaultMaxBytes() {
        return MemoryBudget::instance().sliceBytes(MemoryBudget::Slice::ExcerptTiles);
    }
    static constexpr std::size_t kDefaultMaxBytes = 64 * 1024 * 1024; // legacy per-cache ceiling

    using RenderReadyCallback =
        std::function<void(const std::string& excerptId, uint64_t requestId)>;

    using StrokeProvider = std::function<void(const std::string& docId, std::size_t pageNo,
                                              const FluidCore::Rectangle& cropNormRect,
                                              std::vector<FluidCore::Stroke>& outStrokes)>;
    // Decides whether two document identifiers name the same document.
    //
    // One document is reachable under several ids at once. ExcerptCardNode::sourceDocId() is
    // the tile key, and depending on how the card was made that is an absolute path, a
    // "doc-primary.pdf" synthetic alias (main.cpp seedDemoContent), an "assets/images/..."
    // project-relative path, or a bare document id, while invalidation arrives keyed by
    // whatever the pane reports. Comparing those with `==` matched only by luck, so a crop
    // keyed one way was never invalidated when annotated.
    //
    // The resolver is supplied by the frontend (wired to DocumentPane::matchesDocId, which
    // resolves exact id, canonical path, filesystem equivalence and relative-path-suffix
    // forms). ExcerptTileCache::sameDocument() falls back to the document service's own alias
    // table when this returns false. Kept as a callback so the cache stays free of any
    // document-pane dependency.
    using DocAliasResolver =
        std::function<bool(const std::string& cachedDocId, const std::string& otherDocId)>;

    explicit ExcerptTileCache(PdfDocumentService& docService,
                              std::size_t maxBytes = static_cast<std::size_t>(-1));
    ~ExcerptTileCache();

    ExcerptTileCache(const ExcerptTileCache&) = delete;
    ExcerptTileCache& operator=(const ExcerptTileCache&) = delete;

    void setRenderReadyCallback(RenderReadyCallback cb) { m_onRenderReady = std::move(cb); }
    void setStrokeProvider(StrokeProvider provider) { m_strokeProvider = std::move(provider); }
    void setDocAliasResolver(DocAliasResolver resolver) {
        m_docAliasResolver = std::move(resolver);
    }

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

    void setPinnedCrops(const std::unordered_set<CropIdentity, CropIdentityHash>& pinned) {
        m_pinnedCrops = pinned;
    }

    void cancelRequest(uint64_t requestId);
    void cancelDocumentRequests(const std::string& docId);
    // Drops every cached tile and cancels every in-flight render for docId, matching
    // through the alias resolver.
    void invalidate(const std::string& docId);

    // Evicts cached crop tiles for a specific crop region (used when card is resized).
    // Purges purgeTier while retaining the single lowest resident tier that is not purgeTier.
    void invalidateCrop(const std::string& docId, std::size_t pageNo,
                        const FluidCore::Rectangle& normRect, LodTier purgeTier);

    // Evicts cached crop tiles for docId and pageNo intersecting changedNormRect.
    //
    // Also cancels in-flight renders for the intersecting region. Without that, a render
    // dispatched before the edit completes afterwards and inserts a surface built from
    // the pre-edit stroke snapshot (the provider is called synchronously at dispatch),
    // and because requestCropAsync() refuses to re-dispatch a key that is still in
    // m_inFlightKeys the card is left permanently stale.
    void invalidateSpatial(const std::string& docId, std::size_t pageNo,
                           const FluidCore::Rectangle& changedNormRect);

    void clear();

    // Ejects least-recently-used crop tiles until currentBytes() <= targetBytes. Used by
    // the idle trim, which must release surfaces *before* _heapmin() can return anything
    // to the OS. Returns the bytes reclaimed.
    std::size_t trimToBytes(std::size_t targetBytes);

    std::size_t currentBytes() const { return m_currentBytes; }
    std::size_t maxBytes() const { return m_maxBytes; }
    void setMaxBytes(std::size_t maxBytes) { m_maxBytes = maxBytes; }
    std::size_t skippedRasters() const { return m_skippedRasters.load(); }
    std::size_t droppedRasters() const { return m_droppedRasters.load(); }

    std::size_t size() const { return m_lruList.size(); }

    struct ExcerptCropInfo {
        std::string docId;
        std::size_t pageNo = 0;
        LodTier tier = LodTier::Standard;
        int width = 0;
        int height = 0;
        std::size_t bytes = 0;
    };

    struct ExcerptTileCacheStats {
        std::size_t entryCount = 0;
        std::size_t currentBytes = 0;
        std::size_t maxBytes = 0;
        std::size_t activeRequests = 0;
        std::size_t skippedRasters = 0;
        std::size_t droppedRasters = 0;
        std::unordered_map<int, std::size_t> tierCounts;
        std::vector<ExcerptCropInfo> residentCrops;
    };

    ExcerptTileCacheStats getStats() const;
    void dumpStats(const std::string& tag) const;

  private:
    struct CacheNode {
        CropCacheKey key;
        CropIdentity cropId;
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
        std::vector<FluidCore::Stroke> intersectingStrokes;
        ExcerptTileCache* cache = nullptr;
        std::weak_ptr<std::atomic<bool>> aliveToken;
        std::shared_ptr<std::atomic<bool>> cancelToken;
    };

    struct InFlightEntry {
        uint64_t requestId = 0;
        std::shared_ptr<std::atomic<bool>> cancelToken;
    };

    void evict(std::size_t incomingBytes);

    // Drops every cached LoD tier of the same crop region, whatever its tier. A single
    // card visited at several zoom levels would otherwise retain one surface per tier
    // (up to 4x its necessary bytes) until generic byte eviction happens to reach them.
    // Called from insert() so the resolution the cache holds always matches the
    // resolution most recently requested.
    std::size_t evictSiblingTiers(const CropCacheKey& key);
    static void asyncWorkerFunc(gpointer data, gpointer userData);
    static gboolean onRenderCompletedIdle(gpointer data);

    // True when two document identifiers refer to the same document. Exact match first so
    // the common case stays a cheap string compare, then the frontend-supplied resolver.
    bool sameDocument(const std::string& cachedDocId, const std::string& otherDocId) const;

    PdfDocumentService& m_docService;
    std::size_t m_maxBytes = kDefaultMaxBytes;
    std::size_t m_currentBytes = 0;

    std::list<CacheNode> m_lruList;
    std::unordered_map<CropCacheKey, std::list<CacheNode>::iterator, CropCacheKeyHash> m_lookup;
    std::unordered_set<CropIdentity, CropIdentityHash> m_pinnedCrops;

    GThreadPool* m_threadPool = nullptr;
    std::shared_ptr<std::atomic<bool>> m_alive;
    std::atomic<uint64_t> m_nextRequestId{1};
    std::unordered_set<uint64_t> m_activeRequestIds;
    std::unordered_set<CropCacheKey, CropCacheKeyHash> m_inFlightKeys;
    // In-flight key -> {request id, cancelToken}, so invalidation and stale dispatch
    // can cancel precisely the renders covering an edited or zoom-superseded crop.
    std::unordered_map<CropCacheKey, InFlightEntry, CropCacheKeyHash> m_inFlightIds;
    std::atomic<std::size_t> m_skippedRasters{0};
    std::atomic<std::size_t> m_droppedRasters{0};

    RenderReadyCallback m_onRenderReady;
    StrokeProvider m_strokeProvider;
    DocAliasResolver m_docAliasResolver;
};

} // namespace FluidCoreApp
