#include "services/ExcerptTileCache.h"
#include "geometry/StrokeHitTest.h"
#include "services/PdfDocumentService.h"
#include "services/PdfExportService.h"

#include <algorithm>
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

int testSpatialInvalidation() {
    std::cout << "Running testSpatialInvalidation...\n";
    int failures = 0;

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 1000000);

    cairo_surface_t* s1 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50);
    cairo_surface_t* s2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50);
    cairo_surface_t* s3 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50);

    // Tile 1: doc-A, page 0, rect [0.1, 0.1, 0.3, 0.3] -> spans [0.1, 0.4] in x and y
    CropCacheKey k1 =
        CropCacheKey::fromNormalizedRect("doc-A", 0, {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);
    // Tile 2: doc-A, page 0, rect [0.6, 0.6, 0.3, 0.3] -> spans [0.6, 0.9] in x and y (far away)
    CropCacheKey k2 =
        CropCacheKey::fromNormalizedRect("doc-A", 0, {0.6, 0.6, 0.3, 0.3}, LodTier::Standard);
    // Tile 3: doc-A, page 1, rect [0.1, 0.1, 0.3, 0.3] -> same rect as k1, but page 1
    CropCacheKey k3 =
        CropCacheKey::fromNormalizedRect("doc-A", 1, {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);

    cache.insert(k1, CairoSurfaceHandle(s1, true));
    cache.insert(k2, CairoSurfaceHandle(s2, true));
    cache.insert(k3, CairoSurfaceHandle(s3, true));

    failures += check(cache.size() == 3, "3 items cached initially");

    // Invalidate spatial on doc-A, page 0, overlapping k1 only
    FluidCore::Rectangle changeRect{0.2, 0.2, 0.1, 0.1};
    cache.invalidateSpatial("doc-A", 0, changeRect);

    failures += check(cache.size() == 2, "2 items remaining after spatial invalidation");
    failures += check(!cache.get(k1), "k1 was evicted by spatial invalidation");
    failures += check(static_cast<bool>(cache.get(k2)), "k2 (non-overlapping) was preserved");
    failures += check(static_cast<bool>(cache.get(k3)), "k3 (different page) was preserved");

    return failures;
}

int testStrokeProviderWiring() {
    std::cout << "Running testStrokeProviderWiring...\n";
    int failures = 0;

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 1000000);

    bool providerCalled = false;
    std::string passedDocId;
    std::size_t passedPageNo = 999;
    FluidCore::Rectangle passedCropRect;

    cache.setStrokeProvider([&](const std::string& docId, std::size_t pageNo,
                                const FluidCore::Rectangle& cropNormRect,
                                std::vector<FluidCore::Stroke>& outStrokes) {
        providerCalled = true;
        passedDocId = docId;
        passedPageNo = pageNo;
        passedCropRect = cropNormRect;

        FluidCore::Stroke stroke;
        stroke.id = "test-stroke";
        stroke.points = {{cropNormRect.x, cropNormRect.y}};
        outStrokes.push_back(stroke);
    });

    uint64_t req =
        cache.requestCropAsync("card-mock", "doc-test", 2, {0.1, 0.2, 0.4, 0.3}, 200, 150, 1.0);
    failures += check(req > 0, "requestCropAsync dispatched request with stroke provider");
    failures += check(providerCalled, "StrokeProvider was invoked during requestCropAsync");
    failures += check(passedDocId == "doc-test", "Correct docId passed to provider");
    failures += check(passedPageNo == 2, "Correct pageNo passed to provider");
    failures += check(
        std::abs(passedCropRect.x - 0.1) < 1e-6 && std::abs(passedCropRect.y - 0.2) < 1e-6 &&
            std::abs(passedCropRect.w - 0.4) < 1e-6 && std::abs(passedCropRect.h - 0.3) < 1e-6,
        "Normalized crop coordinates passed directly without geometry alteration");

    return failures;
}

int testNonStandardPageFilteringAndPointToPixelAlignment() {
    std::cout << "Running testNonStandardPageFilteringAndPointToPixelAlignment...\n";
    int failures = 0;

    // Simulate textbook page geometry: 459 x 666 pt
    const double pageWidth = 459.0;
    const double pageHeight = 666.0;

    // Two annotations from actual user session:
    // 1. Highlighter stroke (yellow): y around 506.0 pt
    FluidCore::Stroke highlighter;
    highlighter.id = "highlighter-1";
    highlighter.tool = "highlighter";
    highlighter.color = 0xFFFF00;
    highlighter.width = 16.0;
    highlighter.points = {{229.5, 506.0}, {235.0, 506.0}};

    // 2. Pen stroke (black): y around 535.0 pt
    FluidCore::Stroke pen;
    pen.id = "pen-1";
    pen.tool = "pen";
    pen.color = 0x000000;
    pen.width = 2.0;
    pen.points = {{229.5, 535.0}, {235.0, 535.0}};

    // User's crop rectangle in normalized coordinates [0..1]
    // Normalized y = 0.6542, h = 0.2081
    FluidCore::Rectangle normRect{0.1, 0.6542, 0.8, 0.2081};

    // 1. Verify that when denormalized using authoritative 459 x 666 geometry:
    FluidCore::Rectangle cropPdfRect{normRect.x * pageWidth, normRect.y * pageHeight,
                                     normRect.w * pageWidth, normRect.h * pageHeight};

    // cropPdfRect.y is 0.6542 * 666.0 = 435.6972
    // cropPdfRect.h is 0.2081 * 666.0 = 138.5946
    // [435.6972 .. 574.2918]
    auto hlBounds = FluidCore::computeStrokeBounds(highlighter);
    auto penBounds = FluidCore::computeStrokeBounds(pen);

    bool hlIntersects = FluidCore::rectanglesIntersect(hlBounds, cropPdfRect);
    bool penIntersects = FluidCore::rectanglesIntersect(penBounds, cropPdfRect);

    failures += check(hlIntersects,
                      "Highlighter correctly intersects crop under authoritative 459x666 geometry");
    failures +=
        check(penIntersects, "Pen correctly intersects crop under authoritative 459x666 geometry");

    // Contrast with the old 612 x 792 bug:
    FluidCore::Rectangle badPdfRect{normRect.x * 612.0, normRect.y * 792.0, normRect.w * 612.0,
                                    normRect.h * 792.0};
    bool badHlIntersects = FluidCore::rectanglesIntersect(hlBounds, badPdfRect);
    failures += check(!badHlIntersects,
                      "Proves bug root cause: 612x792 fallback falsely excludes highlighter");

    // 2. Point-to-Pixel Invariant Verification (Regular and Rotated):
    // For regular 459x666:
    int targetW = 367;
    int targetH = 139;

    double expectedPixelX = (229.5 - cropPdfRect.x) * static_cast<double>(targetW) / cropPdfRect.w;
    double expectedPixelY = (506.0 - cropPdfRect.y) * static_cast<double>(targetH) / cropPdfRect.h;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, targetW, targetH);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    // Apply exact Cairo transform pipeline:
    cairo_scale(cr, static_cast<double>(targetW) / cropPdfRect.w,
                static_cast<double>(targetH) / cropPdfRect.h);
    cairo_translate(cr, -cropPdfRect.x, -cropPdfRect.y);
    PdfExportService::renderStroke(cr, highlighter);
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    int sampleX = static_cast<int>(std::round(expectedPixelX));
    int sampleY = static_cast<int>(std::round(expectedPixelY));
    sampleX = std::clamp(sampleX, 0, targetW - 1);
    sampleY = std::clamp(sampleY, 0, targetH - 1);

    const uint32_t* pixels =
        reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surface));
    uint32_t renderedPixel = pixels[sampleY * targetW + sampleX];
    uint32_t backgroundPixel = pixels[0]; // Top-left should be blank

    failures += check(renderedPixel != 0,
                      "Highlighter rendered exactly at expected device pixel coordinate");
    failures += check(backgroundPixel == 0, "Background untouched outside annotation stroke");

    cairo_surface_destroy(surface);

    // 3. Rotated Page Geometry (90° rotated page: 666 x 459):
    const double rotPageW = 666.0;
    const double rotPageH = 459.0;
    FluidCore::Rectangle rotNormRect{0.1, 0.2, 0.5, 0.5};
    FluidCore::Rectangle rotCropPdf{rotNormRect.x * rotPageW, rotNormRect.y * rotPageH,
                                    rotNormRect.w * rotPageW, rotNormRect.h * rotPageH};

    FluidCore::Stroke rotStroke;
    rotStroke.id = "rot-stroke";
    rotStroke.tool = "pen";
    rotStroke.color = 0xFF0000;
    rotStroke.width = 4.0;
    rotStroke.points = {{rotCropPdf.x + rotCropPdf.w * 0.5, rotCropPdf.y + rotCropPdf.h * 0.5}};

    cairo_surface_t* rotSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 200);
    cairo_t* rotCr = cairo_create(rotSurface);
    cairo_set_source_rgba(rotCr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(rotCr);

    cairo_scale(rotCr, 200.0 / rotCropPdf.w, 200.0 / rotCropPdf.h);
    cairo_translate(rotCr, -rotCropPdf.x, -rotCropPdf.y);
    PdfExportService::renderStroke(rotCr, rotStroke);
    cairo_destroy(rotCr);
    cairo_surface_flush(rotSurface);

    const uint32_t* rotPixels =
        reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(rotSurface));
    // Center of 200x200 is (100, 100)
    failures += check(rotPixels[100 * 200 + 100] != 0,
                      "Rotated page annotation lands at target device pixel center (100, 100)");

    cairo_surface_destroy(rotSurface);
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
    totalFailures += testSpatialInvalidation();
    totalFailures += testStrokeProviderWiring();
    totalFailures += testNonStandardPageFilteringAndPointToPixelAlignment();
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
