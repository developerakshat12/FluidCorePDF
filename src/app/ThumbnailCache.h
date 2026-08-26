#pragma once

#include <cstddef>
#include <unordered_map>

#include <cairo.h>
#include <poppler.h>

namespace FluidCoreApp {

// Caches rendered thumbnail Cairo image surfaces to prevent repeated and expensive
// poppler_page_render calls during scroll and draw cycles.
class ThumbnailCache {
  public:
    ThumbnailCache() = default;
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    ThumbnailCache(ThumbnailCache&& other) noexcept;
    ThumbnailCache& operator=(ThumbnailCache&& other) noexcept;

    // Returns the cached thumbnail surface for pageIndex, or nullptr if not yet rendered.
    cairo_surface_t* get(std::size_t pageIndex) const;

    // Renders the PopplerPage into a new Cairo image surface, caches it, and returns the surface.
    cairo_surface_t* renderThumbnail(std::size_t pageIndex, PopplerPage* page, double targetWidth,
                                     double targetHeight);

    // Destroys all cached surfaces and empties the cache.
    void clear();

    std::size_t size() const { return m_surfaces.size(); }

  private:
    std::unordered_map<std::size_t, cairo_surface_t*> m_surfaces;
};

} // namespace FluidCoreApp
