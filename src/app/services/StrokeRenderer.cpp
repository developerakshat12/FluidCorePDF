#include "services/StrokeRenderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace FluidCoreApp {
namespace {

constexpr double kPi = std::numbers::pi_v<double>;

void fillDot(cairo_t* cr, double x, double y, double radius) {
    if (!(radius > 0.0)) {
        return;
    }
    cairo_new_path(cr);
    cairo_arc(cr, x, y, radius, 0.0, 2.0 * kPi);
    cairo_fill(cr);
}

} // namespace

bool isConstantWidthTool(const std::string& tool) {
    return tool == "highlighter";
}

StrokeStabilizer::BezierSegment
buildCentripetalSpan(const std::vector<FluidCore::XoppPoint>& points, std::size_t i) {
    const std::size_t n = points.size();
    const StrokeStabilizer::Point2D p1{points[i].x, points[i].y};
    const StrokeStabilizer::Point2D p2{points[i + 1].x, points[i + 1].y};

    const StrokeStabilizer::Point2D p0 =
        (i == 0) ? StrokeStabilizer::Point2D{2.0 * points[0].x - points[1].x,
                                             2.0 * points[0].y - points[1].y}
                 : StrokeStabilizer::Point2D{points[i - 1].x, points[i - 1].y};

    const StrokeStabilizer::Point2D p3 =
        (i + 2 < n) ? StrokeStabilizer::Point2D{points[i + 2].x, points[i + 2].y}
                    : StrokeStabilizer::Point2D{2.0 * points[n - 1].x - points[n - 2].x,
                                                2.0 * points[n - 1].y - points[n - 2].y};

    return StrokeStabilizer::centripetalCatmullRomToBezier(p0, p1, p2, p3, 1.0, 1.0);
}

void strokeConstantCairo(cairo_t* cr, const std::vector<FluidCore::XoppPoint>& points,
                         double width) {
    const std::size_t n = points.size();
    if (n == 0) {
        return;
    }

    const double w = FluidCore::renderedWidth(width);
    if (n == 1) {
        fillDot(cr, points[0].x, points[0].y, w * 0.5);
        return;
    }

    cairo_new_path(cr);
    cairo_move_to(cr, points[0].x, points[0].y);
    if (n == 2) {
        // The Catmull-Rom of two points is exactly the chord, so skip the machinery.
        cairo_line_to(cr, points[1].x, points[1].y);
    } else {
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const auto span = buildCentripetalSpan(points, i);
            cairo_curve_to(cr, span.p1.x, span.p1.y, span.p2.x, span.p2.y, span.p3.x, span.p3.y);
        }
    }
    cairo_set_line_width(cr, w);
    cairo_stroke(cr);
}

void renderStrokeGeometry(cairo_t* cr, const FluidCore::Stroke& stroke) {
    if (stroke.points.empty()) {
        return;
    }
    // Width is constant and pressure is deliberately ignored, so a stored pressure vector
    // never changes how a stroke looks.
    strokeConstantCairo(cr, stroke.points, stroke.width);
}

void strokeBezierSpansCairo(cairo_t* cr, const std::vector<StrokeStabilizer::BezierSegment>& spans,
                            double baseWidth) {
    if (spans.empty()) {
        return;
    }
    cairo_new_path(cr);
    cairo_move_to(cr, spans.front().p0.x, spans.front().p0.y);
    for (const auto& span : spans) {
        cairo_curve_to(cr, span.p1.x, span.p1.y, span.p2.x, span.p2.y, span.p3.x, span.p3.y);
    }
    cairo_set_line_width(cr, FluidCore::renderedWidth(baseWidth));
    cairo_stroke(cr);
}

} // namespace FluidCoreApp
