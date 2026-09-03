#include "services/ExcerptTileCache.h"
#include "services/PdfDocumentService.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

using namespace FluidCoreApp;

namespace {

int check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return 1;
    }
    std::cout << "  PASS: " << message << "\n";
    return 0;
}

int testCropCacheKeyQuantizationAndHashing() {
    std::cout << "Running testCropCacheKeyQuantizationAndHashing...\n";
    int failures = 0;

    FluidCore::Rectangle r1{0.123456, 0.654321, 0.400001, 0.250000};
    FluidCore::Rectangle r2{0.123458, 0.654319, 0.400003, 0.249998};

    CropCacheKey k1 = CropCacheKey::fromNormalizedRect("doc-1", 0, r1, LodTier::HiDpi);
    CropCacheKey k2 = CropCacheKey::fromNormalizedRect("doc-1", 0, r2, LodTier::HiDpi);

    failures += check(k1 == k2, "Quantized rectangle matches within precision");

    CropCacheKeyHash hasher;
    failures += check(hasher(k1) == hasher(k2), "Quantized hash matches");

    CropCacheKey k3 = CropCacheKey::fromNormalizedRect("doc-1", 0, r1, LodTier::Retina);
    failures += check(!(k1 == k3), "Different LoD tiers produce distinct keys");

    return failures;
}

int testLoDTierCalculations() {
    std::cout << "Running testLoDTierCalculations...\n";
    int failures = 0;

    failures += check(computeLodTierFromZoom(0.20) == LodTier::Overview, "0.20x is Overview tier");
    failures += check(computeLodTierFromZoom(0.50) == LodTier::Standard, "0.50x is Standard tier");
    failures += check(computeLodTierFromZoom(1.00) == LodTier::HiDpi, "1.00x is HiDpi tier");
    failures += check(computeLodTierFromZoom(2.00) == LodTier::Retina, "2.00x is Retina tier");
    failures += check(computeLodTierFromZoom(5.00) == LodTier::Ultra, "5.00x is Ultra tier");

    failures += check(getLodTierScale(LodTier::Overview) == 0.5, "Overview scale is 0.5");
    failures += check(getLodTierScale(LodTier::Standard) == 1.0, "Standard scale is 1.0");
    failures += check(getLodTierScale(LodTier::HiDpi) == 2.0, "HiDpi scale is 2.0");
    failures += check(getLodTierScale(LodTier::Retina) == 4.0, "Retina scale is 4.0");
    failures += check(getLodTierScale(LodTier::Ultra) == 8.0, "Ultra scale is 8.0");

    return failures;
}

int testByteBoundedLruEviction() {
    std::cout << "Running testByteBoundedLruEviction...\n";
    int failures = 0;

    PdfDocumentService docService;
    // Budget = 100 x 100 x 4 bytes * 3 = 120,000 bytes (~3 tiles)
    const std::size_t maxBytes = 120000;
    ExcerptTileCache cache(docService, maxBytes);

    cairo_surface_t* s1 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
    cairo_surface_t* s2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
    cairo_surface_t* s3 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
    cairo_surface_t* s4 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);

    CropCacheKey k1 =
        CropCacheKey::fromNormalizedRect("doc-1", 0, {0, 0, 0.5, 0.5}, LodTier::Standard);
    CropCacheKey k2 =
        CropCacheKey::fromNormalizedRect("doc-1", 1, {0, 0, 0.5, 0.5}, LodTier::Standard);
    CropCacheKey k3 =
        CropCacheKey::fromNormalizedRect("doc-1", 2, {0, 0, 0.5, 0.5}, LodTier::Standard);
    CropCacheKey k4 =
        CropCacheKey::fromNormalizedRect("doc-1", 3, {0, 0, 0.5, 0.5}, LodTier::Standard);

    cache.insert(k1, CairoSurfaceHandle(s1, true));
    cache.insert(k2, CairoSurfaceHandle(s2, true));
    cache.insert(k3, CairoSurfaceHandle(s3, true));

    failures += check(cache.size() == 3, "Cache has 3 items before eviction");
    failures += check(cache.currentBytes() <= maxBytes, "Bytes bounded under maxBytes");

    // Insert 4th item -> should evict k1 (LRU)
    cache.insert(k4, CairoSurfaceHandle(s4, true));

    failures += check(cache.size() == 3, "Cache still has 3 items after eviction");
    failures += check(!cache.get(k1), "k1 was evicted");
    failures += check(static_cast<bool>(cache.get(k2)), "k2 is still present");
    failures += check(static_cast<bool>(cache.get(k3)), "k3 is still present");
    failures += check(static_cast<bool>(cache.get(k4)), "k4 is still present");
    failures += check(cache.currentBytes() <= maxBytes, "Bytes bounded after eviction");

    return failures;
}

int testDocumentInvalidationAndCancellation() {
    std::cout << "Running testDocumentInvalidationAndCancellation...\n";
    int failures = 0;

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 1000000);

    cairo_surface_t* s1 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50);
    cairo_surface_t* s2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50);

    CropCacheKey k1 =
        CropCacheKey::fromNormalizedRect("doc-A", 0, {0, 0, 0.5, 0.5}, LodTier::Standard);
    CropCacheKey k2 =
        CropCacheKey::fromNormalizedRect("doc-B", 0, {0, 0, 0.5, 0.5}, LodTier::Standard);

    cache.insert(k1, CairoSurfaceHandle(s1, true));
    cache.insert(k2, CairoSurfaceHandle(s2, true));

    failures += check(cache.size() == 2, "2 items cached");

    cache.invalidate("doc-A");
    failures += check(cache.size() == 1, "1 item remaining after invalidating doc-A");
    failures += check(!cache.get(k1), "doc-A item was purged");
    failures += check(static_cast<bool>(cache.get(k2)), "doc-B item preserved");

    docService.cancelDocumentRequests("doc-A");
    failures += check(docService.isDocumentCancelled("doc-A"), "doc-A marked cancelled in service");
    failures += check(!docService.isDocumentCancelled("doc-B"), "doc-B not cancelled");

    return failures;
}

int testZeroLeakRefcounting() {
    std::cout << "Running testZeroLeakRefcounting...\n";
    int failures = 0;

    cairo_surface_t* raw = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
    failures += check(cairo_surface_get_reference_count(raw) == 1, "Initial ref count is 1");

    {
        CairoSurfaceHandle h1(raw, false); // references raw -> refcount 2
        failures += check(cairo_surface_get_reference_count(raw) == 2, "Ref count increased to 2");

        CairoSurfaceHandle h2 = h1; // copy -> refcount 3
        failures += check(cairo_surface_get_reference_count(raw) == 3, "Ref count increased to 3");
    }

    failures += check(cairo_surface_get_reference_count(raw) == 1,
                      "Ref count restored to 1 after handles destroyed");
    cairo_surface_destroy(raw);

    return failures;
}

int testRealPdfCropRendering() {
    std::cout << "Running testRealPdfCropRendering...\n";
    int failures = 0;

    const std::string testPdf = "/mnt/d/study material/FIN F414 - FRAM/FRAMTextBook.pdf";
    if (!std::filesystem::exists(testPdf)) {
        std::cout << "  SKIPPED: Real PDF not found\n";
        return 0;
    }

    PdfDocumentService docService;
    docService.registerMainDocument(testPdf, nullptr, testPdf);
    docService.registerMainDocument("FRAMTextBook.pdf", nullptr, testPdf);

    ExcerptTileCache cache(docService, 128 * 1024 * 1024);

    CairoSurfaceHandle surf =
        docService.renderBackgroundCrop(testPdf, 0, {0.1, 0.1, 0.5, 0.5}, 400, 300);
    failures +=
        check(static_cast<bool>(surf), "renderBackgroundCrop rendered surface successfully");
    if (surf) {
        failures += check(surf.width() == 400, "surface width is 400");
        failures += check(surf.height() == 300, "surface height is 300");
    }

    // Now test asynchronous request
    uint64_t req =
        cache.requestCropAsync("card-test", testPdf, 0, {0.1, 0.1, 0.5, 0.5}, 200, 150, 1.0);
    failures += check(req > 0, "requestCropAsync dispatched request");

    return failures;
}

} // namespace

int main() {
    std::cout << "=== Running ExcerptTileCacheTest Suite ===\n";
    int totalFailures = 0;

    totalFailures += testCropCacheKeyQuantizationAndHashing();
    totalFailures += testLoDTierCalculations();
    totalFailures += testByteBoundedLruEviction();
    totalFailures += testDocumentInvalidationAndCancellation();
    totalFailures += testZeroLeakRefcounting();
    totalFailures += testRealPdfCropRendering();

    if (totalFailures == 0) {
        std::cout << "All ExcerptTileCache tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << "ExcerptTileCacheTest failed with " << totalFailures << " errors!\n";
        return 1;
    }
}
