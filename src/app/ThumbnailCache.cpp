#include "ThumbnailCache.h"

#include <algorithm>
#include <cmath>

namespace FluidCoreApp {

ThumbnailCache::~ThumbnailCache() {
    clear();
}

ThumbnailCache::ThumbnailCache(ThumbnailCache&& other) noexcept
    : m_surfaces(std::move(other.m_surfaces)) {
    other.m_surfaces.clear();
}

ThumbnailCache& ThumbnailCache::operator=(ThumbnailCache&& other) noexcept {
    if (this != &other) {
        clear();
        m_surfaces = std::move(other.m_surfaces);
        other.m_surfaces.clear();
    }
    return *this;
}

cairo_surface_t* ThumbnailCache::get(std::size_t pageIndex) const {
    auto it = m_surfaces.find(pageIndex);
    return (it != m_surfaces.end()) ? it->second : nullptr;
}

cairo_surface_t* ThumbnailCache::renderThumbnail(std::size_t pageIndex, PopplerPage* page,
                                                 double targetWidth, double targetHeight) {
    if (!page) {
        return nullptr;
    }

    const int width = std::max(1, static_cast<int>(std::round(targetWidth)));
    const int height = std::max(1, static_cast<int>(std::round(targetHeight)));

    double pageWidth = 0.0;
    double pageHeight = 0.0;
    poppler_page_get_size(page, &pageWidth, &pageHeight);
    if (pageWidth <= 0.0)
        pageWidth = 1.0;
    if (pageHeight <= 0.0)
        pageHeight = 1.0;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return nullptr;
    }

    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    const double scaleX = static_cast<double>(width) / pageWidth;
    const double scaleY = static_cast<double>(height) / pageHeight;
    cairo_scale(cr, scaleX, scaleY);

    poppler_page_render(page, cr);
    cairo_destroy(cr);

    auto it = m_surfaces.find(pageIndex);
    if (it != m_surfaces.end()) {
        if (it->second) {
            cairo_surface_destroy(it->second);
        }
        it->second = surface;
    } else {
        m_surfaces[pageIndex] = surface;
    }

    return surface;
}

void ThumbnailCache::clear() {
    for (auto& [pageIdx, surface] : m_surfaces) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }
    m_surfaces.clear();
}

} // namespace FluidCoreApp
