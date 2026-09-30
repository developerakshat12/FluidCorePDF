#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace FluidCoreApp {

struct TileSizingPolicy {
    static constexpr int kMaxTileDimension = 1536;
    static constexpr std::size_t kMaxTileBytes = 4 * 1024 * 1024; // 4 MiB ceiling

    struct SizingResult {
        int width = 1;
        int height = 1;
        double scale = 1.0;
    };

    static SizingResult computeAspectPreservingDimensions(double cropW, double cropH, int targetW,
                                                          int targetH) {
        if (cropW <= 0.0 || cropH <= 0.0) {
            return {1, 1, 1.0};
        }

        // 1. Uniform initial scale from caller's bounding box constraints
        double s =
            std::min(static_cast<double>(targetW) / cropW, static_cast<double>(targetH) / cropH);

        // 2. Uniform reduction under max dimension ceiling
        s = std::min(s, static_cast<double>(kMaxTileDimension) / cropW);
        s = std::min(s, static_cast<double>(kMaxTileDimension) / cropH);

        // 3. Uniform reduction under 4 MiB byte ceiling
        const double maxPixelArea = static_cast<double>(kMaxTileBytes) / 4.0;
        const double pixelArea = (cropW * s) * (cropH * s);
        if (pixelArea > maxPixelArea) {
            s *= std::sqrt(maxPixelArea / pixelArea);
        }

        // 4. Floor at 1 px (not 16, preserving aspect on thin crops — A1)
        int w = std::max(1, static_cast<int>(std::round(cropW * s)));
        int h = std::max(1, static_cast<int>(std::round(cropH * s)));

        // 5. Post-rounding overflow guard (A2)
        while (static_cast<std::size_t>(w) * h * 4 > kMaxTileBytes) {
            if (w >= h && w > 1) {
                --w;
            } else if (h > 1) {
                --h;
            } else {
                break;
            }
        }

        return {w, h, s};
    }
};

} // namespace FluidCoreApp
