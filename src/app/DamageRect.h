#pragma once

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace FluidCoreApp {

// Pure C++20 geometry for computing integer bounding rectangles for inking damage.
// Used with gtk_widget_queue_draw_area to perform partial-region invalidation
// and guarantee <= 20ms inking latency without full-window repaints.
class DamageRect {
  public:
    struct Point2D {
        double x = 0.0;
        double y = 0.0;
    };

    struct DamageBox {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    static DamageBox computePointDamage(Point2D p, double strokeWidth, double padding = 4.0) {
        const double radius = std::max(1.0, (strokeWidth * 0.5) + padding);
        const int x = static_cast<int>(std::floor(p.x - radius));
        const int y = static_cast<int>(std::floor(p.y - radius));
        const int w = static_cast<int>(std::ceil(radius * 2.0));
        const int h = static_cast<int>(std::ceil(radius * 2.0));
        return {x, y, std::max(1, w), std::max(1, h)};
    }

    static DamageBox computeSegmentDamage(Point2D p0, Point2D p1, double strokeWidth,
                                          double padding = 4.0) {
        const double radius = std::max(1.0, (strokeWidth * 0.5) + padding);
        const double minX = std::min(p0.x, p1.x) - radius;
        const double minY = std::min(p0.y, p1.y) - radius;
        const double maxX = std::max(p0.x, p1.x) + radius;
        const double maxY = std::max(p0.y, p1.y) + radius;

        const int x = static_cast<int>(std::floor(minX));
        const int y = static_cast<int>(std::floor(minY));
        const int w = static_cast<int>(std::ceil(maxX - minX));
        const int h = static_cast<int>(std::ceil(maxY - minY));

        return {x, y, std::max(1, w), std::max(1, h)};
    }

    // Computes bounding box covering the convex hull of a cubic Bezier curve's 4 control points.
    // Guarantees all curved bulges and pressure expansion are fully invalidated.
    static DamageBox computeBezierDamage(Point2D b0, Point2D b1, Point2D b2, Point2D b3,
                                         double strokeWidth, double padding = 4.0) {
        const double radius = std::max(1.0, (strokeWidth * 0.5) + padding);
        const double minX = std::min({b0.x, b1.x, b2.x, b3.x}) - radius;
        const double minY = std::min({b0.y, b1.y, b2.y, b3.y}) - radius;
        const double maxX = std::max({b0.x, b1.x, b2.x, b3.x}) + radius;
        const double maxY = std::max({b0.y, b1.y, b2.y, b3.y}) + radius;

        const int x = static_cast<int>(std::floor(minX));
        const int y = static_cast<int>(std::floor(minY));
        const int w = static_cast<int>(std::ceil(maxX - minX));
        const int h = static_cast<int>(std::ceil(maxY - minY));

        return {x, y, std::max(1, w), std::max(1, h)};
    }
};

} // namespace FluidCoreApp
