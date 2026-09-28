#include "ExcerptTileCache.h"
#include "MemoryTelemetry.h"
#include "geometry/StrokeHitTest.h"
#include "services/PdfExportService.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace FluidCoreApp {

LodTier computeLodTierFromZoom(double canvasZoom) {
    if (canvasZoom < 0.35) {
        return LodTier::Overview;
    } else if (canvasZoom < 0.85) {
        return LodTier::Standard;
    } else if (canvasZoom < 1.75) {
        return LodTier::HiDpi;
    } else if (canvasZoom < 3.5) {
        return LodTier::Retina;
    } else {
        return LodTier::Ultra;
    }
}

double getLodTierScale(LodTier tier) {
    switch (tier) {
    case LodTier::Overview:
        return 0.5;
    case LodTier::Standard:
        return 1.0;
    case LodTier::HiDpi:
        return 2.0;
    case LodTier::Retina:
        return 4.0;
    case LodTier::Ultra:
        return 8.0;
    }
    return 1.0;
}

CropCacheKey CropCacheKey::fromNormalizedRect(const std::string& docId, std::size_t pageNo,
                                              const FluidCore::Rectangle& normRect, LodTier tier) {
    CropCacheKey key;
    key.docId = docId;
    key.pageNo = pageNo;
    key.xNorm = static_cast<uint16_t>(std::clamp(normRect.x, 0.0, 1.0) * 65535.0 + 0.5);
    key.yNorm = static_cast<uint16_t>(std::clamp(normRect.y, 0.0, 1.0) * 65535.0 + 0.5);
    key.wNorm = static_cast<uint16_t>(std::clamp(normRect.w, 0.0, 1.0) * 65535.0 + 0.5);
    key.hNorm = static_cast<uint16_t>(std::clamp(normRect.h, 0.0, 1.0) * 65535.0 + 0.5);
    key.tier = tier;
    return key;
}

struct AsyncRenderResult {
    uint64_t requestId = 0;
    std::string excerptId;
    std::string docId;
    CropCacheKey cacheKey;
    CairoSurfaceHandle surface;
    ExcerptTileCache* cache = nullptr;
    std::weak_ptr<std::atomic<bool>> aliveToken;
};

ExcerptTileCache::ExcerptTileCache(PdfDocumentService& docService, std::size_t maxBytes)
    : m_docService(docService),
      m_maxBytes(maxBytes == static_cast<std::size_t>(-1) ? defaultMaxBytes() : maxBytes),
      m_alive(std::make_shared<std::atomic<bool>>(true)) {
    GError* error = nullptr;
    m_threadPool = g_thread_pool_new(asyncWorkerFunc, this, 2, FALSE, &error);
    if (error) {
        std::cerr << "[ExcerptTileCache] Failed to create GThreadPool: " << error->message
                  << std::endl;
        g_error_free(error);
        m_threadPool = nullptr;
    }
}

ExcerptTileCache::~ExcerptTileCache() {
    if (m_alive) {
        *m_alive = false;
    }
    if (m_threadPool) {
        g_thread_pool_free(m_threadPool, TRUE, TRUE);
        m_threadPool = nullptr;
    }
    clear();
}

CairoSurfaceHandle ExcerptTileCache::get(const CropCacheKey& key) {
    auto it = m_lookup.find(key);
    if (it == m_lookup.end()) {
        return CairoSurfaceHandle{};
    }

    m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
    return it->second->surface;
}

CairoSurfaceHandle ExcerptTileCache::getBestAvailableSurface(const std::string& docId,
                                                             std::size_t pageNo,
                                                             const FluidCore::Rectangle& normRect) {
    static const LodTier preferenceOrder[] = {LodTier::HiDpi, LodTier::Standard, LodTier::Retina,
                                              LodTier::Overview, LodTier::Ultra};

    for (LodTier tier : preferenceOrder) {
        CropCacheKey key = CropCacheKey::fromNormalizedRect(docId, pageNo, normRect, tier);
        auto it = m_lookup.find(key);
        if (it != m_lookup.end()) {
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
            return it->second->surface;
        }
    }

    return CairoSurfaceHandle{};
}

void ExcerptTileCache::evict(std::size_t incomingBytes) {
    while (!m_lruList.empty() && m_currentBytes + incomingBytes > m_maxBytes) {
        auto& victim = m_lruList.back();
        m_currentBytes -= victim.bytes;
        m_lookup.erase(victim.key);
        m_lruList.pop_back();
    }
}

std::size_t ExcerptTileCache::evictSiblingTiers(const CropCacheKey& key) {
    // Collect first: erasing from m_lookup while iterating it would invalidate the
    // iterator, and every erase is a hash lookup over a 4-byte-payload key.
    std::vector<CropCacheKey> doomed;
    for (const auto& node : m_lruList) {
        if (node.key.tier != key.tier && node.key.sameCropAs(key)) {
            doomed.push_back(node.key);
        }
    }

    std::size_t reclaimed = 0;
    for (const auto& victimKey : doomed) {
        auto it = m_lookup.find(victimKey);
        if (it == m_lookup.end()) {
            continue;
        }
        reclaimed += it->second->bytes;
        m_currentBytes -= it->second->bytes;
        m_lruList.erase(it->second);
        m_lookup.erase(it);
    }
    return reclaimed;
}

void ExcerptTileCache::insert(const CropCacheKey& key, CairoSurfaceHandle handle) {
    if (!handle) {
        return;
    }

    auto it = m_lookup.find(key);
    if (it != m_lookup.end()) {
        m_currentBytes -= it->second->bytes;
        m_currentBytes += handle.byteSize();
        it->second->surface = handle;
        it->second->bytes = handle.byteSize();
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        // Re-rendering at the same tier can still leave a higher tier resident from an
        // earlier zoom excursion, so the sweep runs on both paths.
        evictSiblingTiers(key);
        return;
    }

    std::size_t bytes = handle.byteSize();

    // Retire other resolutions of this crop before generic LRU eviction, otherwise the
    // byte budget is spent on stale tiers while evicting tiles other cards still need.
    evictSiblingTiers(key);

    evict(bytes);

    CacheNode node;
    node.key = key;
    node.surface = handle;
    node.bytes = bytes;

    m_lruList.push_front(std::move(node));
    m_lookup[key] = m_lruList.begin();
    m_currentBytes += bytes;
}

CairoSurfaceHandle ExcerptTileCache::renderCropSync(const std::string& docId, std::size_t pageNo,
                                                    const FluidCore::Rectangle& normRect,
                                                    double targetWidthPx, double targetHeightPx,
                                                    PopplerPage* inputPage) {
    PopplerPagePtr pagePtr;
    PopplerPage* page = inputPage;
    if (!page) {
        pagePtr = m_docService.getMainPage(docId, pageNo);
        page = pagePtr.get();
    }

    if (!page) {
        return CairoSurfaceHandle{};
    }

    double origWidth = 0.0, origHeight = 0.0;
    poppler_page_get_size(page, &origWidth, &origHeight);
    if (origWidth <= 0.0 || origHeight <= 0.0) {
        return CairoSurfaceHandle{};
    }

    double cropX = std::clamp(normRect.x, 0.0, 1.0) * origWidth;
    double cropY = std::clamp(normRect.y, 0.0, 1.0) * origHeight;
    double cropW = std::clamp(normRect.w, 0.001, 1.0) * origWidth;
    double cropH = std::clamp(normRect.h, 0.001, 1.0) * origHeight;

    int w = std::clamp(static_cast<int>(std::round(targetWidthPx)), kMinTileDimension,
                       kMaxTileDimension);
    int h = std::clamp(static_cast<int>(std::round(targetHeightPx)), kMinTileDimension,
                       kMaxTileDimension);

    // Byte ceiling on a single tile. The dimension clamp alone permits 1536x1536 ARGB =
    // 9.44 MB, which is ~40% of the whole excerpt slice for one card thumbnail. Cards are
    // ~400 pt wide on the canvas, so a tile is already well oversampled well below this.
    // Scale down uniformly rather than distorting the crop's aspect ratio.
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    if (bytes > kMaxTileBytes) {
        const double shrink =
            std::sqrt(static_cast<double>(kMaxTileBytes) / static_cast<double>(bytes));
        w = std::max(kMinTileDimension, static_cast<int>(w * shrink));
        h = std::max(kMinTileDimension, static_cast<int>(h * shrink));
    }

    std::size_t incomingBytes = static_cast<std::size_t>(w) * h * 4;
    evict(incomingBytes);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        return CairoSurfaceHandle{};
    }

    cairo_t* cr = cairo_create(surface);
    // Opaque white background fill
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Apply scale and translation transformation
    cairo_scale(cr, static_cast<double>(w) / cropW, static_cast<double>(h) / cropH);
    cairo_translate(cr, -cropX, -cropY);

    {
        std::lock_guard<std::mutex> popplerLock(PdfDocumentService::globalPopplerMutex());
        poppler_page_render(page, cr);
    }

    if (m_strokeProvider) {
        std::vector<FluidCore::Stroke> strokes;
        m_strokeProvider(docId, pageNo, normRect, strokes);
        for (const auto& stroke : strokes) {
            PdfExportService::renderStroke(cr, stroke);
        }
    }
    cairo_destroy(cr);

    CairoSurfaceHandle handle(surface, true);
    CropCacheKey key = CropCacheKey::fromNormalizedRect(docId, pageNo, normRect, LodTier::Standard);
    insert(key, handle);
    return handle;
}

uint64_t ExcerptTileCache::requestCropAsync(const std::string& excerptId, const std::string& docId,
                                            std::size_t pageNo,
                                            const FluidCore::Rectangle& normRect,
                                            double cardWidthPt, double cardHeightPt,
                                            double canvasZoom) {
    LodTier tier = computeLodTierFromZoom(canvasZoom);
    CropCacheKey key = CropCacheKey::fromNormalizedRect(docId, pageNo, normRect, tier);

    if (m_lookup.find(key) != m_lookup.end()) {
        return 0; // Already in cache
    }

    if (m_inFlightKeys.find(key) != m_inFlightKeys.end()) {
        return 0; // Already in-flight in background worker
    }

    if (!m_threadPool) {
        return 0;
    }

    double scale = getLodTierScale(tier);
    int targetW = std::clamp(static_cast<int>(std::round(cardWidthPt * scale)), kMinTileDimension,
                             kMaxTileDimension);
    int targetH = std::clamp(static_cast<int>(std::round(cardHeightPt * scale)), kMinTileDimension,
                             kMaxTileDimension);

    uint64_t requestId = m_nextRequestId.fetch_add(1);
    m_activeRequestIds.insert(requestId);
    m_inFlightKeys.insert(key);
    m_inFlightIds.emplace(key, requestId);

    auto* task = new AsyncRenderTask();
    task->requestId = requestId;
    task->excerptId = excerptId;
    task->docId = docId;
    task->pageNo = pageNo;
    task->normRect = normRect;
    task->cacheKey = key;
    task->targetPixelW = targetW;
    task->targetPixelH = targetH;
    task->cache = this;
    task->aliveToken = m_alive;

    if (m_strokeProvider) {
        m_strokeProvider(docId, pageNo, normRect, task->intersectingStrokes);
    }

    GError* error = nullptr;
    g_thread_pool_push(m_threadPool, task, &error);
    if (error) {
        std::cerr << "[ExcerptTileCache] Failed to dispatch async render task: " << error->message
                  << std::endl;
        g_error_free(error);
        delete task;
        m_activeRequestIds.erase(requestId);
        m_inFlightKeys.erase(key);
        m_inFlightIds.erase(key);
        return 0;
    }

    return requestId;
}

void ExcerptTileCache::asyncWorkerFunc(gpointer data, gpointer /*userData*/) {
    auto* task = static_cast<AsyncRenderTask*>(data);
    if (!task || !task->cache) {
        delete task;
        return;
    }

    auto aliveLock = task->aliveToken.lock();
    if (!aliveLock || !*aliveLock) {
        delete task;
        return;
    }

    ExcerptTileCache* cache = task->cache;
    if (cache->m_docService.isDocumentCancelled(task->docId)) {
        delete task;
        return;
    }

    CairoSurfaceHandle surface = cache->m_docService.renderBackgroundCrop(
        task->docId, task->pageNo, task->normRect, task->targetPixelW, task->targetPixelH,
        task->intersectingStrokes);

    auto* result = new AsyncRenderResult();
    result->requestId = task->requestId;
    result->excerptId = task->excerptId;
    result->docId = task->docId;
    result->cacheKey = task->cacheKey;
    result->surface = surface;
    result->cache = cache;
    result->aliveToken = task->aliveToken;

    delete task;

    g_idle_add(onRenderCompletedIdle, result);
}

gboolean ExcerptTileCache::onRenderCompletedIdle(gpointer data) {
    auto* result = static_cast<AsyncRenderResult*>(data);
    if (!result) {
        return G_SOURCE_REMOVE;
    }

    auto aliveLock = result->aliveToken.lock();
    if (aliveLock && *aliveLock && result->cache) {
        ExcerptTileCache* cache = result->cache;
        cache->m_inFlightKeys.erase(result->cacheKey);
        cache->m_inFlightIds.erase(result->cacheKey);
        if (result->surface && cache->m_cancelledRequestIds.count(result->requestId) == 0 &&
            !cache->m_docService.isDocumentCancelled(result->docId)) {
            cache->insert(result->cacheKey, result->surface);
            if (cache->m_onRenderReady) {
                cache->m_onRenderReady(result->excerptId, result->requestId);
            }
        }
        cache->m_activeRequestIds.erase(result->requestId);
        cache->m_cancelledRequestIds.erase(result->requestId);
    }

    delete result;
    return G_SOURCE_REMOVE;
}

void ExcerptTileCache::cancelRequest(uint64_t requestId) {
    m_cancelledRequestIds.insert(requestId);
    m_activeRequestIds.erase(requestId);
}

void ExcerptTileCache::cancelDocumentRequests(const std::string& docId) {
    m_docService.cancelDocumentRequests(docId);
}

bool ExcerptTileCache::sameDocument(const std::string& cachedDocId,
                                    const std::string& otherDocId) const {
    if (cachedDocId == otherDocId) {
        return true;
    }
    if (m_docAliasResolver && (m_docAliasResolver(cachedDocId, otherDocId) ||
                               m_docAliasResolver(otherDocId, cachedDocId))) {
        return true;
    }
    // Last resort: ask the document service, which owns the alias table the renderer uses
    // to fetch pages. A crop keyed by a synthetic alias such as "doc-primary.pdf" is the
    // same document as a pane reporting "doc-primary", and neither DocumentPane's id/path
    // comparison nor a plain string compare can see that. Compared by resolved file path
    // so two genuinely different documents are never conflated.
    const std::string a = m_docService.getFilePath(cachedDocId);
    const std::string b = m_docService.getFilePath(otherDocId);
    return !a.empty() && !b.empty() && a == b;
}

void ExcerptTileCache::invalidate(const std::string& docId) {
    m_docService.cancelDocumentRequests(docId);

    auto it = m_lruList.begin();
    while (it != m_lruList.end()) {
        if (sameDocument(it->key.docId, docId)) {
            m_currentBytes -= it->bytes;
            m_lookup.erase(it->key);
            it = m_lruList.erase(it);
        } else {
            ++it;
        }
    }
}

void ExcerptTileCache::invalidateSpatial(const std::string& docId, std::size_t pageNo,
                                         const FluidCore::Rectangle& changedNormRect) {
    const auto tileIntersects = [&](const CropCacheKey& key) {
        if (key.pageNo != pageNo) {
            return false;
        }
        FluidCore::Rectangle tileNormRect{key.xNorm / 65535.0, key.yNorm / 65535.0,
                                          key.wNorm / 65535.0, key.hNorm / 65535.0};
        return FluidCore::rectanglesIntersect(tileNormRect, changedNormRect, 0.001);
    };

    auto it = m_lruList.begin();
    while (it != m_lruList.end()) {
        if (sameDocument(it->key.docId, docId) && tileIntersects(it->key)) {
            m_currentBytes -= it->bytes;
            m_lookup.erase(it->key);
            it = m_lruList.erase(it);
            continue;
        }
        ++it;
    }

    // Cancel in-flight renders over the same region. The stroke snapshot for a request
    // is taken synchronously at dispatch time, so a render that started before this edit
    // finishes with pre-edit ink. Dropping the request and releasing its in-flight key
    // also lets the next draw dispatch a fresh one; leaving the key registered would
    // make requestCropAsync() return early and pin the card to the stale surface.
    for (auto fit = m_inFlightIds.begin(); fit != m_inFlightIds.end();) {
        if (sameDocument(fit->first.docId, docId) && tileIntersects(fit->first)) {
            m_cancelledRequestIds.insert(fit->second);
            m_activeRequestIds.erase(fit->second);
            m_inFlightKeys.erase(fit->first);
            fit = m_inFlightIds.erase(fit);
        } else {
            ++fit;
        }
    }
}

std::size_t ExcerptTileCache::trimToBytes(std::size_t targetBytes) {
    const std::size_t before = m_currentBytes;
    // Drop the LRU tail until the target is met. Surfaces are released on eviction, so
    // the backing stores are freed here and _heapmin() can then hand the pages back.
    while (m_currentBytes > targetBytes && !m_lruList.empty()) {
        CacheNode& victim = m_lruList.back();
        m_currentBytes -= victim.bytes;
        m_lookup.erase(victim.key);
        m_lruList.pop_back();
    }
    return before - m_currentBytes;
}

void ExcerptTileCache::clear() {
    m_cancelledRequestIds.insert(m_activeRequestIds.begin(), m_activeRequestIds.end());
    m_activeRequestIds.clear();
    m_inFlightKeys.clear();
    m_inFlightIds.clear();
    m_lookup.clear();
    m_lruList.clear();
    m_currentBytes = 0;
}

ExcerptTileCache::ExcerptTileCacheStats ExcerptTileCache::getStats() const {
    ExcerptTileCacheStats stats;
    stats.entryCount = m_lruList.size();
    stats.currentBytes = m_currentBytes;
    stats.maxBytes = m_maxBytes;
    stats.activeRequests = m_activeRequestIds.size();

    for (const auto& node : m_lruList) {
        int tierInt = static_cast<int>(node.key.tier);
        stats.tierCounts[tierInt]++;
        stats.residentCrops.push_back(ExcerptCropInfo{node.key.docId, node.key.pageNo,
                                                      node.key.tier, node.surface.width(),
                                                      node.surface.height(), node.bytes});
    }
    return stats;
}

void ExcerptTileCache::dumpStats(const std::string& tag) const {
    auto stats = getStats();
    static const char* tierNames[] = {"Overview(0.5x)", "Standard(1x)", "HiDpi(2x)", "Retina(4x)",
                                      "Ultra(8x)"};
    std::string tierBreakdown = "";
    for (const auto& [tierInt, count] : stats.tierCounts) {
        if (!tierBreakdown.empty())
            tierBreakdown += ", ";
        const char* name = (tierInt >= 0 && tierInt <= 4) ? tierNames[tierInt] : "Unknown";
        tierBreakdown += std::string(name) + ": " + std::to_string(count);
    }
    if (tierBreakdown.empty()) {
        tierBreakdown = "none";
    }

    MemoryTelemetry::log("[ExcerptTileCache] === " + tag +
                         " === " + "Entries: " + std::to_string(stats.entryCount) +
                         " | Bytes: " + MemoryTelemetry::formatMB(stats.currentBytes) + "/" +
                         MemoryTelemetry::formatMB(stats.maxBytes) +
                         " | Active Req: " + std::to_string(stats.activeRequests) + " | Tiers: [" +
                         tierBreakdown + "]");
}

} // namespace FluidCoreApp
