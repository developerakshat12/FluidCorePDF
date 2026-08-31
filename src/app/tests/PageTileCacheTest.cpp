#include "services/PageTileCache.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << "\n";
        std::abort();
    }
}

FluidCoreApp::CairoSurfaceHandle createDummySurface(int width, int height) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    return FluidCoreApp::CairoSurfaceHandle(surface, /*takeOwnership=*/true);
}

} // namespace

using FluidCoreApp::CairoSurfaceHandle;
using FluidCoreApp::PageTileCache;

void testSurfaceHandleRefcounting() {
    CairoSurfaceHandle h1 = createDummySurface(100, 100);
    expect(h1.width() == 100, "width should be 100");
    expect(h1.height() == 100, "height should be 100");
    expect(h1.byteSize() == 100 * 100 * 4, "byteSize should be 40000");

    // Copy constructor should bump refcount safely
    CairoSurfaceHandle h2 = h1;
    expect(h2.get() == h1.get(), "h2 should share the same underlying surface");

    // Move constructor
    CairoSurfaceHandle h3 = std::move(h2);
    expect(h3.get() == h1.get(), "h3 should hold the surface");
    expect(h2.get() == nullptr, "h2 should be empty after move");

    std::cout << "[PASS] testSurfaceHandleRefcounting\n";
}

void testLruEvictionAndPromotion() {
    // 100x100 ARGB32 surface = 40,000 bytes.
    // Set budget for 3 pages = 120,000 bytes, maxPages = 3.
    const std::size_t pageSize = 100 * 100 * 4;
    PageTileCache cache(pageSize * 3, 3);

    expect(cache.size() == 0, "initial size should be 0");
    expect(cache.currentBytes() == 0, "initial bytes should be 0");

    // Insert pages 0, 1, 2
    cache.insert(0, createDummySurface(100, 100));
    cache.insert(1, createDummySurface(100, 100));
    cache.insert(2, createDummySurface(100, 100));

    expect(cache.size() == 3, "cache size should be 3");
    expect(cache.currentBytes() == pageSize * 3, "bytes should be 3 pages");

    // Access page 0 -> promotes page 0 to MRU (LRU order is now: 1 (oldest), 2, 0 (newest))
    auto h0 = cache.get(0);
    expect(h0.get() != nullptr, "page 0 should be present");

    // Insert page 3 -> should evict page 1 (since page 0 was promoted)
    cache.insert(3, createDummySurface(100, 100));
    expect(cache.size() == 3, "cache size should stay at 3");
    expect(cache.get(1).get() == nullptr, "page 1 should have been evicted");
    expect(cache.get(0).get() != nullptr, "page 0 should still be cached");
    expect(cache.get(2).get() != nullptr, "page 2 should still be cached");
    expect(cache.get(3).get() != nullptr, "page 3 should be cached");

    std::cout << "[PASS] testLruEvictionAndPromotion\n";
}

void testPinnedPageProtection() {
    const std::size_t pageSize = 100 * 100 * 4;
    PageTileCache cache(pageSize * 2, 2);

    cache.insert(0, createDummySurface(100, 100));
    cache.insert(1, createDummySurface(100, 100));

    // Pin page 0 (which is at the LRU tail)
    cache.setPinnedPages({0});

    // Insert page 2 -> should evict unpinned page 1 instead of pinned page 0
    cache.insert(2, createDummySurface(100, 100));
    expect(cache.get(0).get() != nullptr, "pinned page 0 must NOT be evicted");
    expect(cache.get(1).get() == nullptr, "unpinned page 1 should be evicted");
    expect(cache.get(2).get() != nullptr, "new page 2 should be in cache");

    std::cout << "[PASS] testPinnedPageProtection\n";
}

void testByteBudgetAndClear() {
    PageTileCache cache(1000000, 10);
    expect(cache.maxBytes() == 1000000, "maxBytes check");
    expect(cache.maxPages() == 10, "maxPages check");

    cache.insert(5, createDummySurface(100, 100));
    expect(cache.size() == 1, "size check");
    expect(cache.currentBytes() == 40000, "bytes check");

    cache.invalidate(5);
    expect(cache.size() == 0, "size after invalidate");
    expect(cache.currentBytes() == 0, "bytes after invalidate");

    cache.insert(6, createDummySurface(100, 100));
    cache.clear();
    expect(cache.size() == 0, "size after clear");
    expect(cache.currentBytes() == 0, "bytes after clear");

    std::cout << "[PASS] testByteBudgetAndClear\n";
}

int main() {
    testSurfaceHandleRefcounting();
    testLruEvictionAndPromotion();
    testPinnedPageProtection();
    testByteBudgetAndClear();
    std::cout << "All PageTileCache tests passed successfully!\n";
    return 0;
}
