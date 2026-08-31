#include "ExcerptTileCache.h"

#include <algorithm>
#include <cmath>
#include <iostream>

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
};

ExcerptTileCache::ExcerptTileCache(PdfDocumentService& docService, std::size_t maxBytes)
    : m_docService(docService), m_maxBytes(maxBytes) {
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
    clear();
    if (m_threadPool) {
        g_thread_pool_free(m_threadPool, TRUE, TRUE);
        m_threadPool = nullptr;
    }
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
        return;
    }

    std::size_t bytes = handle.byteSize();
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

    poppler_page_render(page, cr);
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

    GError* error = nullptr;
    g_thread_pool_push(m_threadPool, task, &error);
    if (error) {
        std::cerr << "[ExcerptTileCache] Failed to dispatch async render task: " << error->message
                  << std::endl;
        g_error_free(error);
        delete task;
        m_activeRequestIds.erase(requestId);
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

    ExcerptTileCache* cache = task->cache;
    if (cache->m_docService.isDocumentCancelled(task->docId)) {
        delete task;
        return;
    }

    CairoSurfaceHandle surface = cache->m_docService.renderBackgroundCrop(
        task->docId, task->pageNo, task->normRect, task->targetPixelW, task->targetPixelH);

    if (!surface) {
        delete task;
        return;
    }

    auto* result = new AsyncRenderResult();
    result->requestId = task->requestId;
    result->excerptId = task->excerptId;
    result->docId = task->docId;
    result->cacheKey = task->cacheKey;
    result->surface = surface;
    result->cache = cache;

    delete task;

    g_idle_add(onRenderCompletedIdle, result);
}

gboolean ExcerptTileCache::onRenderCompletedIdle(gpointer data) {
    auto* result = static_cast<AsyncRenderResult*>(data);
    if (!result) {
        return G_SOURCE_REMOVE;
    }

    ExcerptTileCache* cache = result->cache;
    if (cache) {
        if (cache->m_cancelledRequestIds.count(result->requestId) == 0 &&
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

void ExcerptTileCache::invalidate(const std::string& docId) {
    m_docService.cancelDocumentRequests(docId);

    auto it = m_lruList.begin();
    while (it != m_lruList.end()) {
        if (it->key.docId == docId) {
            m_currentBytes -= it->bytes;
            m_lookup.erase(it->key);
            it = m_lruList.erase(it);
        } else {
            ++it;
        }
    }
}

void ExcerptTileCache::clear() {
    m_cancelledRequestIds.insert(m_activeRequestIds.begin(), m_activeRequestIds.end());
    m_activeRequestIds.clear();
    m_lookup.clear();
    m_lruList.clear();
    m_currentBytes = 0;
}

} // namespace FluidCoreApp
