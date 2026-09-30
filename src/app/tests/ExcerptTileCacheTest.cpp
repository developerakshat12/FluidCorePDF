#include "services/ExcerptTileCache.h"
#include "geometry/StrokeHitTest.h"
#include "services/PdfDocumentService.h"
#include "services/PdfExportService.h"
#include "services/TileSizing.h"
#include "workspace/CardLayoutEngine.h"

#include <algorithm>
#include <cairo-pdf.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace FluidCoreApp;

namespace {

std::string createSyntheticPdf(const std::string& path, double widthPt = 612.0,
                               double heightPt = 792.0, int numPages = 3) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    cairo_surface_t* surface = cairo_pdf_surface_create(path.c_str(), widthPt, heightPt);
    cairo_t* cr = cairo_create(surface);

    for (int p = 0; p < numPages; ++p) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);

        cairo_set_source_rgb(cr, 0.1, 0.2, 0.3);
        cairo_rectangle(cr, 50, 50, 200, 100);
        cairo_fill(cr);

        cairo_show_page(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return path;
}

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

    // Inserting HiDpi preserves Standard as a fallback tier to prevent placeholder flicker
    cache.insert(
        hidpi, CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(static_cast<bool>(cache.get(standard)),
                      "Standard tier preserved as fallback when HiDpi inserted");
    failures += check(static_cast<bool>(cache.get(hidpi)), "HiDpi tier present");
    failures += check(static_cast<bool>(cache.get(neighbour)), "Neighbouring crop survives");
    failures += check(cache.size() == 3, "Standard fallback + HiDpi + Neighbour cached");
    failures += check(cache.currentBytes() == 3 * tileBytes, "3 tiles resident");

    // Inserting Retina evicts HiDpi (non-fallback sibling) while retaining Standard as fallback
    cache.insert(retina, CairoSurfaceHandle(
                             cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(!cache.get(hidpi), "HiDpi tier dropped when Retina inserted");
    failures += check(static_cast<bool>(cache.get(retina)), "Retina tier present");
    failures += check(static_cast<bool>(cache.get(standard)), "Standard tier retained as fallback");
    failures += check(cache.size() == 3, "Still three entries (Standard + Retina + Neighbour)");
    failures += check(cache.currentBytes() == 3 * tileBytes, "3 tiles resident");

    // Re-inserting the same tier it already holds must not evict itself.
    cache.insert(retina, CairoSurfaceHandle(
                             cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100), true));
    failures += check(static_cast<bool>(cache.get(retina)), "Same-tier re-insert keeps the tile");
    failures += check(static_cast<bool>(cache.get(neighbour)), "Neighbouring crop still resident");
    failures += check(cache.size() == 3, "Same-tier re-insert does not duplicate");

    return failures;
}

int testTileByteCeiling() {
    std::cout << "Running testTileByteCeiling...\n";
    int failures = 0;

    failures +=
        check(TileSizingPolicy::kMaxTileBytes == 4 * 1024 * 1024, "Per-tile byte ceiling is 4 MB");
    const std::size_t unclamped = static_cast<std::size_t>(TileSizingPolicy::kMaxTileDimension) *
                                  TileSizingPolicy::kMaxTileDimension * 4;
    failures += check(unclamped > TileSizingPolicy::kMaxTileBytes,
                      "Byte ceiling is stricter than the dimension clamp");

    // Behaviorally assert computeAspectPreservingDimensions on oversized bounds
    auto res = TileSizingPolicy::computeAspectPreservingDimensions(1000.0, 1000.0, 3000, 3000);
    failures += check(res.width <= TileSizingPolicy::kMaxTileDimension, "Width clamped at 1536");
    failures += check(res.height <= TileSizingPolicy::kMaxTileDimension, "Height clamped at 1536");
    failures += check(static_cast<std::size_t>(res.width) * res.height * 4 <=
                          TileSizingPolicy::kMaxTileBytes,
                      "Pixel bytes <= 4 MB");

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

    std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "FluidCore_Test_Excerpt";
    std::filesystem::create_directories(tempDir);
    std::string testPdf = (tempDir / "synthetic_test.pdf").string();
    createSyntheticPdf(testPdf, 612.0, 792.0, 3);

    PdfDocumentService docService;
    docService.registerMainDocument(testPdf, nullptr, testPdf);
    docService.registerMainDocument("synthetic_test.pdf", nullptr, testPdf);

    ExcerptTileCache cache(docService, 128 * 1024 * 1024);

    CairoSurfaceHandle surf =
        docService.renderBackgroundCrop(testPdf, 0, {0.1, 0.1, 0.5, 0.5}, 400, 300);
    failures +=
        check(static_cast<bool>(surf), "renderBackgroundCrop rendered surface successfully");
    if (surf) {
        failures +=
            check(surf.width() <= 400 && surf.height() <= 300, "surface fits within 400x300");
        failures += check(surf.width() == 400 || surf.height() == 300,
                          "at least one axis hits bounding box");
        const double cropAspect = (0.5 * 612.0) / (0.5 * 792.0); // 0.7727
        const double surfAspect = static_cast<double>(surf.width()) / surf.height();
        failures += check(std::abs(surfAspect - cropAspect) < 0.02, "aspect ratio is preserved");
    }

    // Now test asynchronous request
    uint64_t req =
        cache.requestCropAsync("card-test", testPdf, 0, {0.1, 0.1, 0.5, 0.5}, 200, 150, 1.0);
    failures += check(req > 0, "requestCropAsync dispatched request");

    std::error_code ec;
    std::filesystem::remove(testPdf, ec);
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

// A crop is keyed by a synthetic alias such as "doc-primary.pdf" (main.cpp seedDemoContent,
// and the id registered with the document service), while the pane reports "doc-primary" and
// has no file path at all. Every tier of DocumentPane::matchesDocId fails on that pair: Tier 1
// needs exact equality, Tiers 2 and 4 need a non-empty path, and Tier 3 needs a directory
// component. The crop therefore rendered with zero annotation strokes, so the page drew but
// no ink ever appeared, and symmetrically the invalidation never evicted the tile.
int testSyntheticAliasInvalidation() {
    std::cout << "Running testSyntheticAliasInvalidation...\n";
    int failures = 0;

    PdfDocumentService docService;
    // The service is the alias authority. Register the same file under the pane's own id
    // and under the synthetic alias the cards are keyed by.
    docService.registerMainDocument("doc-primary", nullptr, "D:/docs/paper.pdf");
    docService.registerMainDocument("doc-primary.pdf", nullptr, "D:/docs/paper.pdf");

    failures +=
        check(docService.hasDocument("doc-primary.pdf"), "service resolves the synthetic alias");
    failures += check(docService.hasDocument("doc-primary"), "service resolves the pane's own id");
    failures +=
        check(!docService.hasDocument("totally-unrelated-doc"), "service rejects an unknown id");

    ExcerptTileCache cache(docService, 1000000);
    // No alias resolver installed: this is the plain production wiring, and the cache must
    // still recognise the two ids as one document via the service.
    CropCacheKey kAlias = CropCacheKey::fromNormalizedRect("doc-primary.pdf", 0,
                                                           {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);
    CropCacheKey kFar = CropCacheKey::fromNormalizedRect("doc-primary.pdf", 0, {0.7, 0.7, 0.2, 0.2},
                                                         LodTier::Standard);
    cache.insert(kAlias,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    cache.insert(kFar,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    failures += check(cache.size() == 2, "2 items cached initially");

    // Invalidation arrives keyed by the pane's id, exactly as notifyAnnotationChangedSpatial
    // sends it when the pane has no file path.
    cache.invalidateSpatial("doc-primary", 0, {0.2, 0.2, 0.1, 0.1});

    failures += check(cache.size() == 1, "aliased tile evicted across the doc-primary alias pair");
    failures += check(!cache.get(kAlias), "the alias-keyed tile was the one evicted");
    failures += check(static_cast<bool>(cache.get(kFar)), "non-overlapping tile preserved");

    // Two genuinely different documents must not be conflated by the file-path fallback.
    ExcerptTileCache other(docService, 1000000);
    CropCacheKey kOther =
        CropCacheKey::fromNormalizedRect("other.pdf", 0, {0.1, 0.1, 0.3, 0.3}, LodTier::Standard);
    other.insert(kOther,
                 CairoSurfaceHandle(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 50), true));
    other.invalidateSpatial("doc-primary.pdf", 0, {0.2, 0.2, 0.1, 0.1});
    failures += check(static_cast<bool>(other.get(kOther)),
                      "an id the service cannot resolve is not evicted by alias fallback");

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

    auto rotSizing =
        TileSizingPolicy::computeAspectPreservingDimensions(rotCropPdf.w, rotCropPdf.h, 200, 200);
    int targetRotW = rotSizing.width;
    int targetRotH = rotSizing.height;

    cairo_surface_t* rotSurface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, targetRotW, targetRotH);
    cairo_t* rotCr = cairo_create(rotSurface);
    cairo_set_source_rgba(rotCr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(rotCr);

    double rotScale = static_cast<double>(targetRotW) / rotCropPdf.w;
    cairo_scale(rotCr, rotScale, rotScale);
    cairo_translate(rotCr, -rotCropPdf.x, -rotCropPdf.y);
    PdfExportService::renderStroke(rotCr, rotStroke);
    cairo_destroy(rotCr);
    cairo_surface_flush(rotSurface);

    const uint32_t* rotPixels =
        reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(rotSurface));
    int centerRotX = targetRotW / 2;
    int centerRotY = targetRotH / 2;
    failures +=
        check(rotPixels[centerRotY * targetRotW + centerRotX] != 0,
              "Rotated page annotation lands at target device pixel center under uniform scaling");

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

int testTileAspectPreservedAtEveryTier() {
    std::cout << "Running testTileAspectPreservedAtEveryTier...\n";
    int failures = 0;

    struct CropSpec {
        double w;
        double h;
    };
    std::vector<CropSpec> crops = {
        {400.0, 300.0}, // 4:3
        {160.0, 90.0},  // 16:9
        {500.0, 500.0}, // 1:1
        {300.0, 400.0}, // 3:4
        {5.0, 700.0}    // extreme sliver (A1 test case)
    };

    struct TargetBox {
        int w;
        int h;
    };
    std::vector<TargetBox> targets = {
        {100, 75},    // Overview
        {400, 300},   // Standard
        {800, 600},   // HiDpi
        {1600, 1200}, // UltraHiDpi / Retina
        {2000, 2000}  // Extreme box
    };

    for (const auto& crop : crops) {
        for (const auto& target : targets) {
            auto sizing = TileSizingPolicy::computeAspectPreservingDimensions(crop.w, crop.h,
                                                                              target.w, target.h);
            int outW = sizing.width;
            int outH = sizing.height;

            failures += check(outW >= 1 && outH >= 1,
                              "Output dimensions are at least 1 pixel (no zero-dimension surface)");
            failures += check(outW <= 1536 && outH <= 1536,
                              "Output dimensions obey 1536 max dimension ceiling");
            failures +=
                check(static_cast<std::size_t>(outW) * outH * 4 <= TileSizingPolicy::kMaxTileBytes,
                      "Output dimensions obey 4 MiB byte ceiling");

            if (crop.w >= 50.0 && crop.h >= 50.0) {
                double targetAspect = crop.w / crop.h;
                double outAspect = static_cast<double>(outW) / outH;
                failures += check(std::abs(outAspect - targetAspect) / targetAspect < 0.02,
                                  "Aspect ratio preserved to within 2% rounding tolerance");
            }
        }
    }

    return failures;
}

int testRetinaTierRespectsDimensionAndByteCeiling() {
    std::cout << "Running testRetinaTierRespectsDimensionAndByteCeiling...\n";
    int failures = 0;

    double cropW = 448.0;
    double cropH = 336.0;
    int requestedW = static_cast<int>(cropW * 4.0); // 1792
    int requestedH = static_cast<int>(cropH * 4.0); // 1344

    auto sizing =
        TileSizingPolicy::computeAspectPreservingDimensions(cropW, cropH, requestedW, requestedH);
    int w = sizing.width;
    int h = sizing.height;

    failures += check(w <= TileSizingPolicy::kMaxTileDimension, "Width clamped to <= 1536");
    failures += check(h <= TileSizingPolicy::kMaxTileDimension, "Height clamped to <= 1536");
    std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    failures +=
        check(bytes <= TileSizingPolicy::kMaxTileBytes, "Bytes strictly clamped to <= 4 MiB");

    double cropAspect = cropW / cropH;
    double tileAspect = static_cast<double>(w) / h;
    failures += check(std::abs(tileAspect - cropAspect) / cropAspect < 0.01,
                      "Aspect ratio strictly preserved under uniform scaling");

    auto sqSizing = TileSizingPolicy::computeAspectPreservingDimensions(1000.0, 1000.0, 2000, 2000);
    failures += check(static_cast<std::size_t>(sqSizing.width) * sqSizing.height * 4 <=
                          TileSizingPolicy::kMaxTileBytes,
                      "Square crop near 4 MiB boundary strictly respects 4 MiB ceiling");

    return failures;
}

int testAsyncPathEnforcesPerTileByteCeiling() {
    std::cout << "Running testAsyncPathEnforcesPerTileByteCeiling...\n";
    int failures = 0;

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "FluidCore_Async_Test";
    std::filesystem::create_directories(tempDir);
    std::string pdfPath = (tempDir / "synthetic_async.pdf").string();
    createSyntheticPdf(pdfPath, 612.0, 792.0, 2);

    PdfDocumentService docService;
    docService.registerMainDocument("doc-async-test", nullptr, pdfPath);

    ExcerptTileCache cache(docService, 24 * 1024 * 1024);

    FluidCore::Rectangle normRect{0.1, 0.1, 0.8, 0.8};
    uint64_t reqId =
        cache.requestCropAsync("card-async-1", "doc-async-test", 0, normRect, 1000.0, 800.0, 2.0);
    failures += check(reqId != 0, "Async render task dispatched successfully");

    CropCacheKey expectedKey =
        CropCacheKey::fromNormalizedRect("doc-async-test", 0, normRect, LodTier::Retina);

    auto start = std::chrono::steady_clock::now();
    while (!cache.get(expectedKey) &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CairoSurfaceHandle handle = cache.get(expectedKey);
    failures +=
        check(static_cast<bool>(handle), "Async render completed and tile was inserted into cache");
    if (handle) {
        failures += check(handle.width() <= TileSizingPolicy::kMaxTileDimension,
                          "Async tile width respects max dimension ceiling");
        failures += check(handle.height() <= TileSizingPolicy::kMaxTileDimension,
                          "Async tile height respects max dimension ceiling");
        failures += check(handle.byteSize() <= TileSizingPolicy::kMaxTileBytes,
                          "Async tile strictly respects 4 MiB ceiling");
    }

    std::error_code ec;
    std::filesystem::remove(pdfPath, ec);
    return failures;
}

int testVisibleCropPinnedAgainstEviction() {
    std::cout << "Running testVisibleCropPinnedAgainstEviction...\n";
    int failures = 0;

    PdfDocumentService docService;
    std::size_t budget = 6 * 1024 * 1024;
    ExcerptTileCache cache(docService, budget);

    std::vector<CropCacheKey> keys;
    std::vector<FluidCore::Rectangle> rects = {{0.0, 0.0, 0.2, 0.2}, {0.2, 0.0, 0.2, 0.2},
                                               {0.4, 0.0, 0.2, 0.2}, {0.6, 0.0, 0.2, 0.2},
                                               {0.8, 0.0, 0.2, 0.2}, {0.0, 0.5, 0.2, 0.2}};

    for (int i = 0; i < 6; ++i) {
        CropCacheKey k = CropCacheKey::fromNormalizedRect("doc-1", 0, rects[i], LodTier::Standard);
        keys.push_back(k);
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 512);
        cache.insert(k, CairoSurfaceHandle(s, true));
    }

    failures += check(cache.size() == 6, "6 tiles resident in cache totaling 6 MiB");

    std::unordered_set<CropIdentity, CropIdentityHash> pinned;
    for (int i = 0; i < 3; ++i) {
        pinned.insert(CropIdentity::fromNormalizedRect("doc-1", 0, rects[i]));
    }
    cache.setPinnedCrops(pinned);

    FluidCore::Rectangle r7{0.2, 0.5, 0.2, 0.2};
    CropCacheKey k7 = CropCacheKey::fromNormalizedRect("doc-1", 0, r7, LodTier::Standard);
    cairo_surface_t* s7 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 512);
    cache.insert(k7, CairoSurfaceHandle(s7, true));

    for (int i = 0; i < 3; ++i) {
        failures += check(static_cast<bool>(cache.get(keys[i])),
                          "Pinned crop was preserved against LRU eviction");
    }
    failures += check(!cache.get(keys[3]), "Oldest unpinned tile (keys[3]) was evicted");
    failures += check(static_cast<bool>(cache.get(k7)), "Newly inserted tile is present");
    failures += check(cache.currentBytes() <= budget, "Cache stayed within budget");

    return failures;
}

int testIdleBudgetDoesNotEvictPinnedCrops() {
    std::cout << "Running testIdleBudgetDoesNotEvictPinnedCrops...\n";
    int failures = 0;

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 24 * 1024 * 1024);

    std::vector<FluidCore::Rectangle> rects = {
        {0.0, 0.0, 0.2, 0.2}, {0.2, 0.0, 0.2, 0.2}, {0.4, 0.0, 0.2, 0.2}, {0.6, 0.0, 0.2, 0.2}};

    std::unordered_set<CropIdentity, CropIdentityHash> pinned;
    for (int i = 0; i < 4; ++i) {
        CropCacheKey k = CropCacheKey::fromNormalizedRect("doc-1", 0, rects[i], LodTier::HiDpi);
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 724, 724);
        cache.insert(k, CairoSurfaceHandle(s, true));
        pinned.insert(CropIdentity::fromNormalizedRect("doc-1", 0, rects[i]));
    }

    cache.setPinnedCrops(pinned);
    failures += check(cache.size() == 4, "4 tiles resident before idle trim");

    cache.trimToBytes(6 * 1024 * 1024);

    failures += check(cache.size() == 4, "All 4 pinned tiles retained across idle trim");
    for (int i = 0; i < 4; ++i) {
        CropCacheKey k = CropCacheKey::fromNormalizedRect("doc-1", 0, rects[i], LodTier::HiDpi);
        failures += check(static_cast<bool>(cache.get(k)), "Pinned tile survived idle trim");
    }

    return failures;
}

int testCanonicalGeometryConsistency() {
    std::cout << "Running testCanonicalGeometryConsistency...\n";
    int failures = 0;

    FluidCore::Rectangle cardBounds{50.0, 80.0, 320.0, 240.0};
    FluidCore::Rectangle worldRect =
        FluidCore::CardLayoutEngine::cardImageBodyWorldRect(cardBounds);

    failures += check(worldRect.x == cardBounds.x + 24.0, "Body world rect X has 24pt left offset");
    failures += check(worldRect.y == cardBounds.y + 34.0, "Body world rect Y has 34pt top offset");
    failures += check(worldRect.w == cardBounds.w - FluidCore::CardLayoutEngine::kTotalChromeWidth,
                      "Body world rect W matches cardW - kTotalChromeWidth (32)");
    failures += check(worldRect.h == cardBounds.h - FluidCore::CardLayoutEngine::kTotalChromeHeight,
                      "Body world rect H matches cardH - kTotalChromeHeight (40)");

    double zoom = 1.5;
    double originX = 10.0;
    double originY = 20.0;
    FluidCore::Rectangle screenRect =
        FluidCore::CardLayoutEngine::cardImageBodyScreenRect(cardBounds, originX, originY, zoom);

    double expectedScreenX = (cardBounds.x - originX) * zoom + 24.0 * zoom;
    double expectedScreenY = (cardBounds.y - originY) * zoom + 34.0 * zoom;
    double expectedScreenW = (cardBounds.w - FluidCore::CardLayoutEngine::kTotalChromeWidth) * zoom;
    double expectedScreenH =
        (cardBounds.h - FluidCore::CardLayoutEngine::kTotalChromeHeight) * zoom;

    failures += check(std::abs(screenRect.x - expectedScreenX) < 0.001,
                      "Screen body rect X matches scaled chrome left");
    failures += check(std::abs(screenRect.y - expectedScreenY) < 0.001,
                      "Screen body rect Y matches scaled chrome top");
    failures += check(std::abs(screenRect.w - expectedScreenW) < 0.001,
                      "Screen body rect W matches scaled body width");
    failures += check(std::abs(screenRect.h - expectedScreenH) < 0.001,
                      "Screen body rect H matches scaled body height");

    FluidCore::Rectangle shortCardBounds{0.0, 0.0, 100.0, 50.0};
    FluidCore::Rectangle shortScreenRect =
        FluidCore::CardLayoutEngine::cardImageBodyScreenRect(shortCardBounds, 0.0, 0.0, 1.0);
    failures += check(std::abs(shortScreenRect.y - (50.0 * 0.35 + 6.0)) < 0.001,
                      "Short card (<60pt) uses top padding = 0.35 * height + 6.0");

    return failures;
}

int testStaleTierCancellationAtDispatch() {
    std::cout << "Running testStaleTierCancellationAtDispatch...\n";
    int failures = 0;

    std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "FluidCore_StaleTier_Test";
    std::filesystem::create_directories(tempDir);
    std::string pdfPath = (tempDir / "stale_test.pdf").string();
    createSyntheticPdf(pdfPath, 612.0, 792.0, 2);

    PdfDocumentService docService;
    docService.registerMainDocument("doc-stale-test", nullptr, pdfPath);

    ExcerptTileCache cache(docService, 24 * 1024 * 1024);

    FluidCore::Rectangle cropRect{0.1, 0.1, 0.5, 0.5};

    // Install StrokeProvider
    std::atomic<int> strokeCalls{0};
    cache.setStrokeProvider([&](const std::string&, std::size_t, const FluidCore::Rectangle&,
                                std::vector<FluidCore::Stroke>& strokes) {
        strokeCalls++;
        FluidCore::Stroke s;
        s.id = "test-stroke";
        strokes.push_back(s);
    });

    // Hold Poppler mutex and occupy both GThreadPool workers with dummy tasks so
    // task 1 provably cannot begin or complete before task 2 is dispatched.
    std::unique_lock<std::mutex> popLock(PdfDocumentService::globalPopplerMutex());
    FluidCore::Rectangle dummyRect1{0.01, 0.01, 0.02, 0.02};
    FluidCore::Rectangle dummyRect2{0.03, 0.03, 0.02, 0.02};
    cache.requestCropAsync("dummy-1", "doc-stale-test", 0, dummyRect1, 50.0, 50.0, 1.0);
    cache.requestCropAsync("dummy-2", "doc-stale-test", 0, dummyRect2, 50.0, 50.0, 1.0);

    // Yield so the 2 workers wake up and block on popLock inside renderBackgroundCrop
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // First dispatch at Standard tier (zoom = 1.0)
    uint64_t req1 =
        cache.requestCropAsync("card-1", "doc-stale-test", 0, cropRect, 300.0, 200.0, 1.0);
    failures += check(req1 != 0, "First async request dispatched at Standard tier");

    // Immediately dispatch at Retina tier (zoom = 2.0) before worker can pick up req1
    uint64_t req2 =
        cache.requestCropAsync("card-1", "doc-stale-test", 0, cropRect, 300.0, 200.0, 2.0);
    failures += check(req2 != 0, "Second async request dispatched at Retina tier");
    failures += check(req2 != req1, "Second request generated distinct request ID");

    // Now release the Poppler lock so workers can resume
    popLock.unlock();

    CropCacheKey keyStandard =
        CropCacheKey::fromNormalizedRect("doc-stale-test", 0, cropRect, LodTier::Standard);
    CropCacheKey keyRetina =
        CropCacheKey::fromNormalizedRect("doc-stale-test", 0, cropRect, LodTier::Retina);

    // Pump GLib loop until Retina completes
    auto start = std::chrono::steady_clock::now();
    while ((!cache.get(keyRetina) || cache.getStats().activeRequests > 0) &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
        g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    failures +=
        check(static_cast<bool>(cache.get(keyRetina)), "Retina tier completed and was inserted");
    // Standard tier should not be in cache (cancelled)
    failures += check(!cache.get(keyStandard), "Stale Standard tier was not inserted into cache");
    // Telemetry split asserts: 1 raster skipped before Poppler, 0 dropped after Poppler
    failures +=
        check(cache.skippedRasters() >= 1, "At least 1 stale raster skipped before Poppler render");
    failures += check(cache.droppedRasters() == 0, "0 rasters dropped after Poppler render");
    // Active requests must have returned to 0
    failures += check(cache.getStats().activeRequests == 0,
                      "activeRequests returned to 0 (no leaked requests)");

    std::error_code ec;
    std::filesystem::remove(pdfPath, ec);
    return failures;
}

int testResizeCommitRetainsFallbackSurface() {
    std::cout << "Running testResizeCommitRetainsFallbackSurface...\n";
    int failures = 0;

    PdfDocumentService docService;
    ExcerptTileCache cache(docService, 24 * 1024 * 1024);

    FluidCore::Rectangle cropRect{0.1, 0.1, 0.4, 0.3};
    CropCacheKey keyStandard =
        CropCacheKey::fromNormalizedRect("doc-resize-1", 0, cropRect, LodTier::Standard);
    cairo_surface_t* sStandard = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 150);
    cache.insert(keyStandard, CairoSurfaceHandle(sStandard, true));

    CropCacheKey keyHiDpi =
        CropCacheKey::fromNormalizedRect("doc-resize-1", 0, cropRect, LodTier::HiDpi);
    cairo_surface_t* sHiDpi = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 300);
    cache.insert(keyHiDpi, CairoSurfaceHandle(sHiDpi, true));

    failures += check(cache.size() == 2, "Initial Standard and HiDpi surfaces resident");

    // Call invalidateCrop with purgeTier = LodTier::HiDpi as WorkspaceView does on resize-commit
    cache.invalidateCrop("doc-resize-1", 0, cropRect, LodTier::HiDpi);

    // 1. The original HiDpi key must be evicted so requestCropAsync can dispatch the new size
    failures += check(!cache.get(keyHiDpi), "HiDpi key evicted so resize can re-render");

    // 2. Standard tier MUST be retained with its authentic tier unchanged (NOT demoted to Overview)
    failures += check(static_cast<bool>(cache.get(keyStandard)),
                      "Standard fallback preserved with authentic tier unchanged");
    failures +=
        check(cache.size() == 1, "Cache holds exactly 1 fallback tile after invalidateCrop");

    // 3. Regression guard for Blocker 1: follow-up requestCropAsync at the same zoom (HiDpi) MUST
    // dispatch a new id (not early-return 0)
    uint64_t newReq = cache.requestCropAsync("card-resize", "doc-resize-1", 0, cropRect, 500.0,
                                             350.0, 2.0); // zoom 2.0 -> HiDpi
    failures +=
        check(newReq != 0,
              "requestCropAsync at HiDpi dispatches a new ID (regression guard for Blocker 1)");

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
    totalFailures += testSyntheticAliasInvalidation();
    totalFailures += testStrokeProviderWiring();
    totalFailures += testNonStandardPageFilteringAndPointToPixelAlignment();
    totalFailures += testZeroLeakRefcounting();
    totalFailures += testRealPdfCropRendering();
    totalFailures += testPdfDocumentServiceAliasAndLegacyPathResolution();
    totalFailures += testTileAspectPreservedAtEveryTier();
    totalFailures += testRetinaTierRespectsDimensionAndByteCeiling();
    totalFailures += testAsyncPathEnforcesPerTileByteCeiling();
    totalFailures += testVisibleCropPinnedAgainstEviction();
    totalFailures += testIdleBudgetDoesNotEvictPinnedCrops();
    totalFailures += testCanonicalGeometryConsistency();
    totalFailures += testStaleTierCancellationAtDispatch();
    totalFailures += testResizeCommitRetainsFallbackSurface();

    if (totalFailures == 0) {
        std::cout << "All ExcerptTileCache tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << "ExcerptTileCacheTest failed with " << totalFailures << " errors!\n";
        return 1;
    }
}
