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

int testTierExclusiveEviction() {
    std::cout << "Running testTierExclusiveEviction...\n";
    int failures = 0;

    PdfDocumentService docService;
    // Generous budget so generic LRU eviction can never be what removes a tile: this
    // test only passes if sibling tiers are retired by crop identity.
    ExcerptTileCache cache(docService, 64 * 1024 * 1024);

    const FluidCore::Rectangle region{0.0, 0.0, 0.4, 0.3};
    const FluidCore::Rectangle otherRegion{0.6, 0.6, 0.3, 0.3};

    CropCacheKey standard = CropCacheKey::fromNormalizedRect("doc-1", 0, region, LodTier::Standard);
    CropCacheKey hidpi = CropCacheKey::fromNormalizedRect("doc-1", 0, region, LodTier::HiDpi);
    CropCacheKey retina = CropCacheKey::fromNormalizedRect("doc-1", 0, region, LodTier::Retina);

    // A different crop on the same page must be untouched by the sweep.
    CropCacheKey neighbour =
        CropCacheKey::fromNormalizedRect("doc-1", 0, otherRegion, LodTier::Standard);

    const std::size_t tileBytes = 100 * 100 * 4;

    cache.insert(standard, CairoSurfaceHandle(
                               cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    cache.insert(neighbour, CairoSurfaceHandle(
                                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(cache.size() == 2, "Two distinct crops cached");
    failures += check(cache.currentBytes() == 2 * tileBytes, "Two tiles resident");

    cache.insert(
        hidpi, CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(!cache.get(standard), "Standard tier dropped when HiDpi inserted");
    failures += check(static_cast<bool>(cache.get(hidpi)), "HiDpi tier present");
    failures += check(static_cast<bool>(cache.get(neighbour)), "Neighbouring crop survives");
    failures += check(cache.size() == 2, "Entry count unchanged by tier swap");
    failures += check(cache.currentBytes() == 2 * tileBytes, "No byte growth from tier swap");

    cache.insert(retina, CairoSurfaceHandle(
                             cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(!cache.get(hidpi), "HiDpi tier dropped when Retina inserted");
    failures += check(static_cast<bool>(cache.get(retina)), "Retina tier present");
    failures += check(cache.size() == 2, "Still only two entries after second swap");
    failures += check(cache.currentBytes() == 2 * tileBytes, "Bytes flat across three zoom levels");

    // Re-inserting the same tier it already holds must not evict itself.
    cache.insert(retina, CairoSurfaceHandle(
                             cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(static_cast<bool>(cache.get(retina)), "Same-tier re-insert keeps the tile");
    failures += check(static_cast<bool>(cache.get(neighbour)), "Neighbouring crop still resident");
    failures += check(cache.size() == 2, "Same-tier re-insert does not duplicate");

    return failures;
}

int testTileByteCeiling() {
    std::cout << "Running testTileByteCeiling...\n";
    int failures = 0;

    failures +=
        check(ExcerptTileCache::kMaxTileBytes == 4 * 1024 * 1024, "Per-tile byte ceiling is 4 MB");
    // The dimension clamp alone would allow 1536*1536*4 = 9.44 MB for one card.
    const std::size_t unclamped = static_cast<std::size_t>(ExcerptTileCache::kMaxTileDimension) *
                                  ExcerptTileCache::kMaxTileDimension * 4;
    failures += check(unclamped > ExcerptTileCache::kMaxTileBytes,
                      "Byte ceiling is stricter than the dimension clamp");

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

// A crop made in this session records the absolute PDF path, but the same card reloaded
// from a saved project records the project-relative path, while annotation invalidation
// always arrives keyed by the pane's absolute path. Verbatim comparison silently matched
// nothing, so on a reloaded project no crop tile was ever evicted and cards kept showing
// pre-annotation imagery.
int testAliasAwareSpatialInvalidation() {
    std::cout << "Running testAliasAwareSpatialInvalidation...\n";
    int failures = 0;

    const std::string kAbsPath = "D:/projects/thesis/docs/paper.pdf";
    const std::string kRelPath = "docs/paper.pdf";

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 1000000);

    // Models DocumentPane::matchesDocId's relative-path-suffix tier: a relative id matches
    // an absolute one when the absolute path ends with "/" + relative.
    cache.setDocAliasResolver([&](const std::string& cachedDocId, const std::string& otherDocId) {
        auto norm = [](std::string s) {
            std::replace(s.begin(), s.end(), '\\', '/');
            while (s.rfind("./", 0) == 0) {
                s.erase(0, 2);
            }
            return s;
        };
        const std::string a = norm(cachedDocId);
        const std::string b = norm(otherDocId);
        if (a == b) {
            return true;
        }
        if (a.size() > b.size() && a.rfind("/" + b) == a.size() - (b.size() + 1)) {
            return true;
        }
        return b.size() > a.size() && b.rfind("/" + a) == b.size() - (a.size() + 1);
    });

    // Tile as it exists after the project was saved and reopened: keyed by relative path.
    CropCacheKey kRel =
        CropCacheKey::fromNormalizedRect(kRelPath, 0, {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);
    // Same page, but a region far from the edit, keyed by the relative path too.
    CropCacheKey kRelFar =
        CropCacheKey::fromNormalizedRect(kRelPath, 0, {0.6, 0.6, 0.3, 0.3}, LodTier::Standard);
    // A tile on another page, which must survive.
    CropCacheKey kRelPage1 =
        CropCacheKey::fromNormalizedRect(kRelPath, 1, {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);
    // A genuinely different document that merely shares a name suffix pattern.
    CropCacheKey kOther = CropCacheKey::fromNormalizedRect("other/docs/paper.pdf", 0,
                                                           {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);

    cache.insert(kRel,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    cache.insert(kRelFar,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    cache.insert(kRelPage1,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    cache.insert(kOther,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    failures += check(cache.size() == 4, "4 items cached initially");

    // Invalidation arrives keyed by the ABSOLUTE path, as DocumentPane produces it.
    FluidCore::Rectangle changeRect{0.2, 0.2, 0.1, 0.1};
    cache.invalidateSpatial(kAbsPath, 0, changeRect);

    failures += check(cache.size() == 3, "one tile evicted by aliased spatial invalidation");
    failures += check(!cache.get(kRel), "relative-path tile was evicted via absolute-path alias");
    failures += check(static_cast<bool>(cache.get(kRelFar)), "non-overlapping tile preserved");
    failures += check(static_cast<bool>(cache.get(kRelPage1)), "different-page tile preserved");
    failures += check(static_cast<bool>(cache.get(kOther)), "unrelated document preserved");

    // Whole-document invalidation must resolve aliases too: it should clear both tiles
    // keyed by the relative path and leave the unrelated document alone.
    cache.invalidate(kAbsPath);
    failures += check(cache.size() == 1, "aliased whole-document invalidation evicted one tile");
    failures += check(!cache.get(kRelFar), "aliased whole-document eviction cleared kRelFar");
    failures += check(!cache.get(kRelPage1), "aliased whole-document eviction cleared kRelPage1");
    failures += check(static_cast<bool>(cache.get(kOther)),
                      "whole-document invalidation did not touch an unrelated document");

    // With no resolver installed, behaviour must stay exactly as before: verbatim only.
    ExcerptTileCache strict(docService, 1000000);
    CropCacheKey kStrict =
        CropCacheKey::fromNormalizedRect(kRelPath, 0, {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);
    strict.insert(
        kStrict, CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    strict.invalidateSpatial(kAbsPath, 0, changeRect);
    failures += check(static_cast<bool>(strict.get(kStrict)),
                      "no resolver installed: aliases are still not matched (unchanged behaviour)");
    strict.invalidateSpatial(kRelPath, 0, changeRect);
    failures += check(!strict.get(kStrict), "no resolver installed: exact id still invalidates");

    return failures;
}

// An async render snapshots the intersecting strokes synchronously at dispatch. If an edit
// lands while that render is in flight, the render finishes with pre-edit ink and inserts
// it, and because requestCropAsync() early-returns while the key is registered in
// m_inFlightKeys the card is pinned to that stale surface forever. Invalidation must
// release the in-flight key so the next draw can dispatch a fresh render.
int testSpatialInvalidationReleasesInFlightRequests() {
    std::cout << "Running testSpatialInvalidationReleasesInFlightRequests...\n";
    int failures = 0;

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 1000000);

    FluidCore::Rectangle cropRect{0.1, 0.1, 0.3, 0.3};

    const uint64_t first =
        cache.requestCropAsync("card-1", "doc-A", 0, cropRect, 200.0, 150.0, 1.0);
    failures += check(first != 0, "first async render was dispatched");

    // Same key while in flight: must be refused, proving the in-flight guard is active.
    const uint64_t blocked =
        cache.requestCropAsync("card-1", "doc-A", 0, cropRect, 200.0, 150.0, 1.0);
    failures += check(blocked == 0, "duplicate in-flight request is refused");

    // Edit lands over the crop while that render is still running.
    cache.invalidateSpatial("doc-A", 0, {0.2, 0.2, 0.1, 0.1});

    // The key must now be dispatchable again; without releasing it this returns 0 and the
    // card is stuck on the stale surface.
    const uint64_t second =
        cache.requestCropAsync("card-1", "doc-A", 0, cropRect, 200.0, 150.0, 1.0);
    failures += check(second != 0, "in-flight key released so a fresh render can be dispatched");
    failures += check(second != first, "re-dispatch produced a new request id");

    // An edit that does not touch the crop must NOT release the in-flight key.
    const uint64_t third =
        cache.requestCropAsync("card-2", "doc-A", 0, {0.7, 0.7, 0.2, 0.2}, 200.0, 150.0, 1.0);
    failures += check(third != 0, "unrelated crop dispatched");
    cache.invalidateSpatial("doc-A", 0, {0.2, 0.2, 0.1, 0.1});
    const uint64_t fourth =
        cache.requestCropAsync("card-2", "doc-A", 0, {0.7, 0.7, 0.2, 0.2}, 200.0, 150.0, 1.0);
    failures += check(fourth == 0, "non-overlapping in-flight render left running");

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

int testPdfDocumentServiceAliasAndLegacyPathResolution() {
    std::cout << "Running testPdfDocumentServiceAliasAndLegacyPathResolution...\n";
    int failures = 0;

    PdfDocumentService docService;
    docService.registerMainDocument("doc-primary", nullptr, "D:/docs/sample.pdf");
    docService.registerMainDocument("sample.pdf", nullptr, "D:/docs/sample.pdf");

    // Test resolving legacy path with assets/images prefix
    failures += check(docService.getFilePath("assets/images/sample.pdf") == "D:/docs/sample.pdf",
                      "Legacy assets/images/sample.pdf resolves to primary document path");
    failures += check(docService.getFilePath("documents/sample.pdf") == "D:/docs/sample.pdf",
                      "documents/sample.pdf resolves to primary document path");
    failures += check(docService.getFilePath("sample.pdf") == "D:/docs/sample.pdf",
                      "Direct filename resolves to primary document path");

    return failures;
}

} // namespace

int main() {
    std::cout << "=== Running ExcerptTileCacheTest Suite ===\n";
    int totalFailures = 0;

    totalFailures += testCropCacheKeyQuantizationAndHashing();
    totalFailures += testLoDTierCalculations();
    totalFailures += testByteBoundedLruEviction();
    totalFailures += testTierExclusiveEviction();
    totalFailures += testTileByteCeiling();
    totalFailures += testDocumentInvalidationAndCancellation();
    totalFailures += testSpatialInvalidation();
    totalFailures += testAliasAwareSpatialInvalidation();
    totalFailures += testSpatialInvalidationReleasesInFlightRequests();
    totalFailures += testStrokeProviderWiring();
    totalFailures += testNonStandardPageFilteringAndPointToPixelAlignment();
    totalFailures += testZeroLeakRefcounting();
    totalFailures += testRealPdfCropRendering();
    totalFailures += testPdfDocumentServiceAliasAndLegacyPathResolution();

    if (totalFailures == 0) {
        std::cout << "All ExcerptTileCache tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << "ExcerptTileCacheTest failed with " << totalFailures << " errors!\n";
        return 1;
    }
}
