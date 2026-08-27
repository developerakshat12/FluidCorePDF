#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace FluidCoreApp {

enum class StabilizerMode {
    None,   // Exact raw stylus input (0 sample lag)
    Smooth, // Centripetal Catmull-Rom + Velocity-Adaptive Deadzone (<= 1 sample lag / 8ms at 125Hz)
    ArtInertia // Physics-based mass-drag smoothing for calligraphy and artistic curves
};

// Pure C++20 stroke stabilizer engine with zero GUI/Poppler dependencies.
// Converts incoming stylus/mouse coordinates into smooth Centripetal Catmull-Rom
// cubic Bezier curves with bounded perceived latency (<= 20ms release budget).
class StrokeStabilizer {
  public:
    struct Point2D {
        double x = 0.0;
        double y = 0.0;

        bool operator==(const Point2D& other) const {
            return std::abs(x - other.x) < 1e-6 && std::abs(y - other.y) < 1e-6;
        }
    };

    struct StabilizedSample {
        Point2D point;
        double pressure = 1.0;
        uint64_t timestamp = 0;
    };

    struct BezierSegment {
        Point2D p0;
        Point2D p1;
        Point2D p2;
        Point2D p3;
        double pressure0 = 1.0;
        double pressure1 = 1.0;
    };

    struct PushResult {
        std::vector<BezierSegment> newlyCommitted;
        Point2D wetTip;
        bool hasWetSegment = false;
    };

    StrokeStabilizer();
    ~StrokeStabilizer() = default;

    void beginStroke(Point2D pt, double pressure, uint64_t timestamp,
                     StabilizerMode mode = StabilizerMode::Smooth);

    PushResult pushPoint(Point2D pt, double pressure, uint64_t timestamp);

    std::vector<BezierSegment> endStroke();

    StabilizerMode mode() const { return m_mode; }
    void setMode(StabilizerMode mode) { m_mode = mode; }

    const std::vector<StabilizedSample>& rawSamples() const { return m_samples; }
    std::size_t sampleCount() const { return m_samples.size(); }

    // Converts 4 control points into an exact Cubic Bezier curve using Centripetal Catmull-Rom
    // (alpha = 0.5). Prevents cusps, self-intersections, and overshoot on irregular digitizer point
    // spacing.
    static BezierSegment centripetalCatmullRomToBezier(Point2D p0, Point2D p1, Point2D p2,
                                                       Point2D p3, double pr1, double pr2);

  private:
    Point2D filterDeadzone(Point2D pt, uint64_t timestamp);
    Point2D filterInertia(Point2D pt);

    StabilizerMode m_mode = StabilizerMode::Smooth;

    std::vector<StabilizedSample> m_samples;
    std::size_t m_committedCount = 0;

    Point2D m_lastFilteredPoint;
    uint64_t m_lastTimestamp = 0;
    double m_lastVelocity = 0.0;

    // Inertia physics state
    Point2D m_inertiaPos;
    Point2D m_inertiaVel{0.0, 0.0};
    double m_mass = 2.0;
    double m_drag = 0.45;

    // Adaptive deadzone config
    double m_deadzoneRadiusBase = 0.75; // pixels
    double m_velocityThreshold = 1.5;   // pixels/ms
};

} // namespace FluidCoreApp
