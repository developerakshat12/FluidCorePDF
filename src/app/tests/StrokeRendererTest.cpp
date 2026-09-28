#include "services/StrokeRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

// CTest runs the RelWithDebInfo configuration, which defines NDEBUG and therefore
// compiles assert() out entirely. A plain assert() would make every check below a
// no-op, so failures are reported explicitly instead.
#define REQUIRE(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "FAILED: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n";            \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

using namespace FluidCoreApp;

constexpr double kPi = std::numbers::pi_v<double>;

// Renders into a transparent ARGB32 surface.
//
// The surface is deliberately left unpainted so that the alpha channel is exactly the
// ink coverage. Painting an opaque background first would make every pixel read as
// alpha 255 and silently turn all the coverage assertions into tautologies.
struct Raster {
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = nullptr;
    int w = 0;
    int h = 0;

    Raster(int width, int height, double scale = 1.0) : w(width), h(height) {
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cr = cairo_create(surface);
        if (scale != 1.0) {
            cairo_scale(cr, scale, scale);
        }
    }
    ~Raster() {
        if (cr) {
            cairo_destroy(cr);
        }
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }
    Raster(const Raster&) = delete;
    Raster& operator=(const Raster&) = delete;

    // Alpha of a device pixel, 0..255.
    unsigned alphaAt(int x, int y) const {
        cairo_surface_flush(surface);
        const uint8_t* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        // ARGB32 is premultiplied and native-endian; read the alpha byte explicitly.
        const uint32_t px = *reinterpret_cast<const uint32_t*>(data + y * stride + 4 * x);
        return (px >> 24) & 0xFF;
    }

    // Bilinearly interpolated alpha at fractional device coordinates.
    //
    // Nearest-neighbour sampling along a ray is a staircase: successive samples land in
    // the same pixel for a whole run, which weights that pixel by its run length rather
    // than by the ray length it actually covers. The resulting bias swings by the better
    // part of a pixel with angle, swamping the sub-pixel variation these tests resolve.
    double alphaBilinear(double fx, double fy) const {
        if (fx < 0.0 || fy < 0.0 || fx > static_cast<double>(w - 1) ||
            fy > static_cast<double>(h - 1)) {
            return 0.0;
        }
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const int x1 = std::min(x0 + 1, w - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const double tx = fx - x0;
        const double ty = fy - y0;
        const double a00 = alphaAt(x0, y0);
        const double a10 = alphaAt(x1, y0);
        const double a01 = alphaAt(x0, y1);
        const double a11 = alphaAt(x1, y1);
        return (a00 * (1.0 - tx) + a10 * tx) * (1.0 - ty) + (a01 * (1.0 - tx) + a11 * tx) * ty;
    }

    // Total ink coverage, counting pixels above the coverage threshold.
    int totalInk() const {
        cairo_surface_flush(surface);
        const uint8_t* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        int count = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint32_t px = *reinterpret_cast<const uint32_t*>(data + y * stride + 4 * x);
                if (((px >> 24) & 0xFF) > 200) {
                    ++count;
                }
            }
        }
        return count;
    }
};

// Alpha-weighted radial centroid offset at a given angle, plus the total ink weight the
// ray accumulated.
//
// Walking outward and recording the last opaque pixel is too coarse to measure
// smoothness: integer pixel rounding alone moves that boundary by more than a pixel,
// swamping the sub-pixel variation this test is looking for. Weighting each sample by its
// own alpha and taking the centroid averages the rounding down.
//
// For a perfectly centred constant-width ring the centroid sits exactly on the nominal
// radius, so the offset is zero at every angle.
struct RadialSample {
    double offset = 0.0;
    double weight = 0.0;
};

RadialSample radialSample(const Raster& raster, double cx, double cy, double angle, double radius,
                          double halfBand) {
    const double ca = std::cos(angle);
    const double sa = std::sin(angle);
    double weighted = 0.0;
    double total = 0.0;
    for (double r = radius - halfBand; r <= radius + halfBand; r += 0.05) {
        const double a = raster.alphaBilinear(cx + r * ca, cy + r * sa);
        if (a <= 0.0) {
            continue;
        }
        weighted += a * (r - radius);
        total += a;
    }
    RadialSample out;
    out.offset = total > 0.0 ? weighted / total : 0.0;
    out.weight = total;
    return out;
}

FluidCore::Stroke makeArc(int segments, double width, const std::string& tool = "pen",
                          std::vector<double> pressures = {}) {
    FluidCore::Stroke s;
    s.tool = tool;
    s.width = width;
    s.color = 0x000000;
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / segments;
        const double a = t * 2.0 * kPi;
        s.points.push_back({40.0 + 30.0 * std::cos(a), 40.0 + 30.0 * std::sin(a)});
    }
    if (!pressures.empty()) {
        s.pressures = std::move(pressures);
    }
    return s;
}

// The core regression this file exists for.
//
// The old renderer flattened every Catmull-Rom span into a fixed number of chords and
// stroked each chord on its own. A fixed chord count is resolution dependent, so the
// painted radius of a circular stroke varied along its length: smooth by accident at one
// zoom, visibly faceted at another, and beaded at the joins by the chained round caps.
void testConstantWidthStrokeHasUniformRadius() {
    Raster raster(96, 96);
    renderStrokeGeometry(raster.cr, makeArc(48, 8.0));

    const double cx = 40.0;
    const double cy = 40.0;
    const double radius = 30.0;
    const double halfWidth = 4.0;

    double minOffset = 1e9;
    double maxOffset = -1e9;
    std::vector<double> allOffsets;
    double minWeight = 1e9;
    for (int i = 0; i < 240; ++i) {
        const RadialSample s =
            radialSample(raster, cx, cy, 2.0 * kPi * i / 240.0, radius, halfWidth + 2.0);
        allOffsets.push_back(s.offset);
        minWeight = std::min(minWeight, s.weight);
        minOffset = std::min(minOffset, s.offset);
        maxOffset = std::max(maxOffset, s.offset);
    }
    const double splineSpread = maxOffset - minOffset;
    std::vector<double> sortedOffsets = allOffsets;
    std::sort(sortedOffsets.begin(), sortedOffsets.end());
    const double splineMedian = sortedOffsets[sortedOffsets.size() / 2];
    std::cout << "  [info] ring centroid offsets " << minOffset << ".." << maxOffset
              << "px (spread " << splineSpread << ", median " << splineMedian << ")\n";

    // Every ray must genuinely have crossed the ink. A ray that missed returns a zero
    // offset with no weight, which would otherwise sit at the centre of the range and
    // quietly flatter the spread.
    REQUIRE(minWeight > 255.0 * 4.0);
    // The measurement floor of this technique is about 0.8px either way: the pixel grid,
    // antialiasing, and the band edges all contribute.
    REQUIRE(splineSpread < 2.0);
    // The ring must be centred on its nominal circle, not merely self-consistent.
    REQUIRE(std::abs(splineMedian) < 0.15);

    // Same circle drawn as an inscribed 48-gon: straight chords, no spline. This is the
    // faceted geometry a fixed-chord renderer produces, and rendering it here proves the
    // assertion above has discriminating power rather than passing everything.
    Raster faceted(96, 96);
    FluidCore::Stroke poly;
    poly.tool = "pen";
    poly.width = 8.0;
    poly.color = 0x000000;
    for (int i = 0; i < 48; ++i) {
        const double a = 2.0 * kPi * i / 48.0;
        poly.points.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a)});
    }
    renderStrokeGeometry(faceted.cr, poly);

    double facetedMin = 1e9;
    double facetedMax = -1e9;
    for (int i = 0; i < 240; ++i) {
        const double offset =
            radialSample(faceted, cx, cy, 2.0 * kPi * i / 240.0, radius, halfWidth + 2.0).offset;
        facetedMin = std::min(facetedMin, offset);
        facetedMax = std::max(facetedMax, offset);
    }
    const double facetedSpread = facetedMax - facetedMin;
    std::cout << "  [info] faceted 48-gon centroid offsets " << facetedMin << ".." << facetedMax
              << "px (spread " << facetedSpread << ")\n";
    REQUIRE(facetedSpread > splineSpread * 2.0);

    std::cout << "  [PASS] testConstantWidthStrokeHasUniformRadius (smooth spread " << splineSpread
              << "px vs faceted " << facetedSpread << "px)\n";
}

// Width must not vary with pressure. This is the behaviour the user asked for: a stroke
// set to 2pt is 2pt along its whole length, including where the pen was pressed hard
// and where it lifted.
void testPressureDoesNotChangeWidth() {
    // A strong taper across a long straight stroke, which previously produced a
    // 0.25x..1.0x ramp.
    std::vector<double> taper;
    for (int i = 0; i <= 40; ++i) {
        taper.push_back(static_cast<double>(i) / 40.0);
    }

    const double baseline = [&] {
        Raster raster(200, 80);
        FluidCore::Stroke flat;
        flat.tool = "pen";
        flat.width = 12.0;
        flat.color = 0x000000;
        flat.points = {{10.0, 40.0}, {60.0, 40.0}, {120.0, 40.0}, {190.0, 40.0}};
        flat.pressures.assign(4, 1.0);
        renderStrokeGeometry(raster.cr, flat);
        return raster.totalInk();
    }();

    const double tapered = [&] {
        Raster raster(200, 80);
        FluidCore::Stroke stroke;
        stroke.tool = "pen";
        stroke.width = 12.0;
        stroke.color = 0x000000;
        stroke.points = {{10.0, 40.0}, {60.0, 40.0}, {120.0, 40.0}, {190.0, 40.0}};
        stroke.pressures = {0.0, 0.0, 1.0, 1.0};
        renderStrokeGeometry(raster.cr, stroke);
        return raster.totalInk();
    }();

    const double extreme = [&] {
        Raster raster(200, 80);
        FluidCore::Stroke stroke;
        stroke.tool = "pen";
        stroke.width = 12.0;
        stroke.color = 0x000000;
        stroke.points = {{10.0, 40.0}, {60.0, 40.0}, {120.0, 40.0}, {190.0, 40.0}};
        stroke.pressures = taper;
        renderStrokeGeometry(raster.cr, stroke);
        return raster.totalInk();
    }();

    std::cout << "  [info] ink coverage: uniform=" << baseline << " tapered=" << tapered
              << " graded=" << extreme << "\n";
    REQUIRE(std::abs(tapered - baseline) / baseline < 0.02);
    REQUIRE(std::abs(extreme - baseline) / baseline < 0.02);
    std::cout << "  [PASS] testPressureDoesNotChangeWidth\n";
}

// A stroke must not thin toward its tail. Under the old taper the ribbon collapsed
// toward the 0.25x floor on pen lift while the round cap kept full radius, which is what
// produced a detached-looking blob at the end of otherwise clean lines.
void testStrokeEndsAtFullWidth() {
    Raster raster(160, 60);
    FluidCore::Stroke stroke;
    stroke.tool = "pen";
    stroke.width = 6.0;
    stroke.color = 0x000000;
    stroke.points = {{20.0, 30.0}, {80.0, 30.0}, {140.0, 30.0}};
    // Full taper: zero pressure at the start, maximum at the end.
    stroke.pressures = {0.0, 0.5, 1.0};
    renderStrokeGeometry(raster.cr, stroke);

    auto thicknessAt = [&](int x) {
        int count = 0;
        for (int y = 0; y < raster.h; ++y) {
            if (raster.alphaAt(x, y) > 200) {
                ++count;
            }
        }
        return count;
    };

    // Sample a few pixels inside each end rather than at the end point itself. A round
    // end cap is centred on the endpoint, so the column at that exact x is the tangent
    // of the cap and holds almost no coverage no matter what the stroke width is.
    const int atStart = thicknessAt(24);
    const int atEnd = thicknessAt(136);
    std::cout << "  [info] thickness start=" << atStart << "px end=" << atEnd << "px\n";
    // Both ends must be at the full nominal 6pt width; the old taper put the tail at 1.5pt.
    REQUIRE(atStart >= 5);
    REQUIRE(atEnd >= 5);
    // And they must agree: no thinning along the stroke.
    REQUIRE(std::abs(atStart - atEnd) <= 1);
    std::cout << "  [PASS] testStrokeEndsAtFullWidth\n";
}

// The selected width must be honoured exactly, so a 2pt line and a 20pt line render in
// that proportion.
void testSelectedWidthIsHonoured() {
    auto measure = [](double width) {
        Raster raster(200, 60);
        FluidCore::Stroke stroke;
        stroke.tool = "pen";
        stroke.width = width;
        stroke.color = 0x000000;
        stroke.points = {{20.0, 30.0}, {180.0, 30.0}};
        stroke.pressures = {1.0, 1.0};
        renderStrokeGeometry(raster.cr, stroke);
        int count = 0;
        for (int y = 0; y < raster.h; ++y) {
            if (raster.alphaAt(100, y) > 200) {
                ++count;
            }
        }
        return count;
    };

    const int thin = measure(2.0);
    const int thick = measure(20.0);
    std::cout << "  [info] measured 2pt=" << thin << "px 20pt=" << thick << "px\n";
    REQUIRE(thin >= 1 && thin <= 3);
    REQUIRE(thick >= 18 && thick <= 22);
    // Roughly proportional, so the number in the toolbar means what it says.
    REQUIRE(thick > thin * 6);
    std::cout << "  [PASS] testSelectedWidthIsHonoured\n";
}

// The stored width is the only input to geometry, so a pressure vector stored in any of
// the three length conventions must not perturb the result.
void testStoredPressureVectorsAreIgnored() {
    auto coverage = [](std::vector<double> pressures) {
        Raster raster(120, 60);
        FluidCore::Stroke stroke;
        stroke.tool = "pen";
        stroke.width = 10.0;
        stroke.color = 0x000000;
        stroke.points = {{20.0, 30.0}, {60.0, 30.0}, {100.0, 30.0}};
        stroke.pressures = std::move(pressures);
        renderStrokeGeometry(raster.cr, stroke);
        return raster.totalInk();
    };

    const int live = coverage({0.1, 0.2, 0.3}); // one per point
    const int xopp = coverage({0.1, 0.2});      // one per segment
    const int none = coverage({});              // absent
    const int mismatched = coverage({0.1, 0.2, 0.3, 0.4, 0.5});
    const int full = coverage({1.0, 1.0, 1.0});

    std::cout << "  [info] coverage live=" << live << " xopp=" << xopp << " none=" << none
              << " mismatched=" << mismatched << " full=" << full << "\n";
    REQUIRE(live == none);
    REQUIRE(xopp == none);
    REQUIRE(mismatched == none);
    REQUIRE(full == none);
    std::cout << "  [PASS] testStoredPressureVectorsAreIgnored\n";
}

// Quality must not depend on zoom. Cairo's own stroker subdivides adaptively, so the
// device-space result is the same regardless of the transform scale.
void testQualityIsZoomInvariant() {
    auto measureSpread = [](double scale) {
        const int segments = static_cast<int>(48 * scale);
        const int size = static_cast<int>(96 * scale);
        const double worldRadius = 30.0 * scale;
        const double worldCenter = 40.0 * scale;

        Raster raster(size, size);
        cairo_scale(raster.cr, scale, scale);

        FluidCore::Stroke stroke;
        stroke.tool = "pen";
        stroke.width = 8.0;
        stroke.color = 0x000000;
        for (int i = 0; i <= segments; ++i) {
            const double a = 2.0 * kPi * i / segments;
            stroke.points.push_back(
                {worldCenter + worldRadius * std::cos(a), worldCenter + worldRadius * std::sin(a)});
        }
        cairo_set_source_rgb(raster.cr, 0.0, 0.0, 0.0);
        renderStrokeGeometry(raster.cr, stroke);

        double minOffset = 1e9;
        double maxOffset = -1e9;
        for (int i = 0; i < 240; ++i) {
            const RadialSample s = radialSample(raster, worldCenter, worldCenter,
                                                2.0 * kPi * i / 240.0, worldRadius, 6.0 * scale);
            minOffset = std::min(minOffset, s.offset);
            maxOffset = std::max(maxOffset, s.offset);
        }
        return maxOffset - minOffset;
    };

    const double at1x = measureSpread(1.0);
    const double at4x = measureSpread(4.0);
    std::cout << "  [info] device-space centroid spread: 1x=" << at1x << "px 4x=" << at4x << "px\n";
    REQUIRE(at4x <= at1x * 1.25);
    std::cout << "  [PASS] testQualityIsZoomInvariant\n";
}

void testDegenerateStrokesDoNotCrash() {
    Raster raster(64, 64);
    cairo_set_source_rgb(raster.cr, 0.0, 0.0, 0.0);

    // Empty.
    renderStrokeGeometry(raster.cr, FluidCore::Stroke{});

    // Single point (a tap).
    {
        FluidCore::Stroke s;
        s.tool = "pen";
        s.width = 6.0;
        s.points = {{20.0, 20.0}};
        s.pressures = {1.0};
        renderStrokeGeometry(raster.cr, s);
    }
    // Two points.
    {
        FluidCore::Stroke s;
        s.tool = "pen";
        s.width = 6.0;
        s.points = {{10.0, 10.0}, {40.0, 40.0}};
        renderStrokeGeometry(raster.cr, s);
    }
    // All points coincident.
    {
        FluidCore::Stroke s;
        s.tool = "pen";
        s.width = 6.0;
        s.points = {{20.0, 20.0}, {20.0, 20.0}, {20.0, 20.0}};
        s.pressures = {1.0, 1.0, 1.0};
        renderStrokeGeometry(raster.cr, s);
    }
    // Zero and negative width must not produce a degenerate path that traps the fill.
    {
        FluidCore::Stroke s;
        s.tool = "pen";
        s.width = 0.0;
        s.points = {{5.0, 50.0}, {55.0, 50.0}};
        renderStrokeGeometry(raster.cr, s);
    }
    {
        FluidCore::Stroke s;
        s.tool = "pen";
        s.width = -4.0;
        s.points = {{5.0, 30.0}, {55.0, 30.0}};
        renderStrokeGeometry(raster.cr, s);
    }

    REQUIRE(cairo_status(raster.cr) == CAIRO_STATUS_SUCCESS);
    std::cout << "  [PASS] testDegenerateStrokesDoNotCrash\n";
}

void testToolWidthPolicy() {
    REQUIRE(isConstantWidthTool("highlighter"));
    REQUIRE(!isConstantWidthTool("pen"));
    REQUIRE(!isConstantWidthTool("eraser"));

    // Every tool draws at its selected width; the highlighter is distinguished by
    // translucency, not by geometry.
    auto coverage = [](const char* tool) {
        Raster raster(100, 100);
        FluidCore::Stroke s;
        s.tool = tool;
        s.width = 12.0;
        s.color = 0x000000;
        for (int i = 0; i <= 32; ++i) {
            const double a = 2.0 * kPi * i / 32.0;
            s.points.push_back({40.0 + 30.0 * std::cos(a), 40.0 + 30.0 * std::sin(a)});
        }
        s.pressures.assign(s.points.size(), 0.5);
        renderStrokeGeometry(raster.cr, s);
        return raster.totalInk();
    };

    const int asHighlighter = coverage("highlighter");
    const int asPen = coverage("pen");
    std::cout << "  [info] coverage highlighter=" << asHighlighter << " pen=" << asPen << "\n";
    REQUIRE(std::abs(asHighlighter - asPen) <= 2);
    std::cout << "  [PASS] testToolWidthPolicy\n";
}

void testBezierSpanPath() {
    Raster raster(120, 80);
    cairo_set_source_rgb(raster.cr, 0.0, 0.0, 0.0);

    std::vector<StrokeStabilizer::BezierSegment> spans;
    for (int i = 0; i < 8; ++i) {
        StrokeStabilizer::BezierSegment seg;
        seg.p0 = {15.0 + i * 12.0, 40.0};
        seg.p1 = {19.0 + i * 12.0, 30.0};
        seg.p2 = {23.0 + i * 12.0, 50.0};
        seg.p3 = {27.0 + i * 12.0, 40.0};
        // Spans arrive carrying pressure from the stabilizer; it must not change the ink.
        seg.pressure0 = 0.0;
        seg.pressure1 = 1.0;
        spans.push_back(seg);
    }
    strokeBezierSpansCairo(raster.cr, spans, 8.0);

    // Empty span list is a no-op, not a crash.
    strokeBezierSpansCairo(raster.cr, {}, 8.0);

    REQUIRE(cairo_status(raster.cr) == CAIRO_STATUS_SUCCESS);

    // The span path must agree with the equivalent point path.
    Raster viaPoints(120, 80);
    FluidCore::Stroke stroke;
    stroke.tool = "pen";
    stroke.width = 8.0;
    stroke.points = {{15.0, 40.0}, {63.0, 40.0}, {111.0, 40.0}};
    renderStrokeGeometry(viaPoints.cr, stroke);
    REQUIRE(viaPoints.totalInk() > 0);
    std::cout << "  [PASS] testBezierSpanPath\n";
}

void testWidthModel() {
    // The selected width is used as-is, above the floor.
    REQUIRE(std::abs(FluidCore::renderedWidth(10.0) - 10.0) < 1e-9);
    REQUIRE(std::abs(FluidCore::renderedWidth(2.0) - 2.0) < 1e-9);

    // Non-positive and NaN widths clamp to the floor rather than producing a
    // zero-area or negative path.
    REQUIRE(FluidCore::renderedWidth(0.0) == FluidCore::kMinStrokeWidthPx);
    REQUIRE(FluidCore::renderedWidth(-3.0) == FluidCore::kMinStrokeWidthPx);
    REQUIRE(std::isfinite(FluidCore::renderedWidth(std::numeric_limits<double>::quiet_NaN())));
    REQUIRE(FluidCore::renderedWidth(std::numeric_limits<double>::quiet_NaN()) > 0.0);

    // Below the floor, the floor applies.
    REQUIRE(FluidCore::renderedWidth(0.1) == FluidCore::kMinStrokeWidthPx);

    // Half width is exactly half.
    REQUIRE(std::abs(FluidCore::renderedHalfWidth(10.0) - 5.0) < 1e-9);
    std::cout << "  [PASS] testWidthModel\n";
}

} // namespace

int main() {
    std::cout << "=== Running StrokeRenderer Unit Tests ===\n";
    testConstantWidthStrokeHasUniformRadius();
    testPressureDoesNotChangeWidth();
    testStrokeEndsAtFullWidth();
    testSelectedWidthIsHonoured();
    testStoredPressureVectorsAreIgnored();
    testQualityIsZoomInvariant();
    testDegenerateStrokesDoNotCrash();
    testToolWidthPolicy();
    testBezierSpanPath();
    testWidthModel();
    std::cout << "=== All StrokeRenderer Tests Passed! ===\n";
    return 0;
}
