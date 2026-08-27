#include "StrokeStabilizer.h"

#include <cmath>

namespace FluidCoreApp {

StrokeStabilizer::StrokeStabilizer() = default;

void StrokeStabilizer::beginStroke(Point2D pt, double pressure, uint64_t timestamp,
                                   StabilizerMode mode) {
    m_mode = mode;
    m_samples.clear();
    m_committedCount = 0;

    m_lastFilteredPoint = pt;
    m_lastTimestamp = timestamp;
    m_lastVelocity = 0.0;

    m_inertiaPos = pt;
    m_inertiaVel = {0.0, 0.0};

    m_samples.push_back({pt, pressure, timestamp});
}

StrokeStabilizer::Point2D StrokeStabilizer::filterDeadzone(Point2D pt, uint64_t timestamp) {
    const double dist = std::hypot(pt.x - m_lastFilteredPoint.x, pt.y - m_lastFilteredPoint.y);
    const double dtMs =
        (timestamp > m_lastTimestamp) ? static_cast<double>(timestamp - m_lastTimestamp) : 0.0;

    double velocity = 0.0;
    if (dtMs >= 1.0) {
        velocity = dist / dtMs;
    } else {
        velocity = m_lastVelocity;
    }

    m_lastVelocity = 0.7 * m_lastVelocity + 0.3 * velocity;
    const double rEff =
        m_deadzoneRadiusBase * std::max(0.0, 1.0 - (m_lastVelocity / m_velocityThreshold));

    if (dist < rEff) {
        return m_lastFilteredPoint;
    }

    m_lastFilteredPoint = pt;
    m_lastTimestamp = timestamp;
    return pt;
}

StrokeStabilizer::Point2D StrokeStabilizer::filterInertia(Point2D pt) {
    const Point2D force = {pt.x - m_inertiaPos.x, pt.y - m_inertiaPos.y};
    m_inertiaVel.x = m_inertiaVel.x * (1.0 - m_drag) + (force.x / m_mass);
    m_inertiaVel.y = m_inertiaVel.y * (1.0 - m_drag) + (force.y / m_mass);
    m_inertiaPos.x += m_inertiaVel.x;
    m_inertiaPos.y += m_inertiaVel.y;
    return m_inertiaPos;
}

StrokeStabilizer::PushResult StrokeStabilizer::pushPoint(Point2D pt, double pressure,
                                                         uint64_t timestamp) {
    Point2D filtered = pt;
    if (m_mode == StabilizerMode::Smooth) {
        filtered = filterDeadzone(pt, timestamp);
    } else if (m_mode == StabilizerMode::ArtInertia) {
        filtered = filterInertia(pt);
    }

    if (!m_samples.empty() && filtered == m_samples.back().point) {
        return PushResult{{}, filtered, true};
    }

    m_samples.push_back({filtered, pressure, timestamp});

    PushResult result;
    result.wetTip = filtered;
    result.hasWetSegment = true;

    if (m_mode == StabilizerMode::None) {
        while (m_committedCount + 1 < m_samples.size()) {
            const Point2D p0 = m_samples[m_committedCount].point;
            const Point2D p1 = m_samples[m_committedCount + 1].point;
            const double pr0 = m_samples[m_committedCount].pressure;
            const double pr1 = m_samples[m_committedCount + 1].pressure;
            result.newlyCommitted.push_back({p0, p0, p1, p1, pr0, pr1});
            m_committedCount++;
        }
        return result;
    }

    // Centripetal Catmull-Rom streaming:
    // When size == 3, synthesize first span [P0, P1] with lookahead P2 and reflected P-1
    if (m_samples.size() == 3 && m_committedCount == 0) {
        const Point2D pMinus1 = {2.0 * m_samples[0].point.x - m_samples[1].point.x,
                                 2.0 * m_samples[0].point.y - m_samples[1].point.y};
        const auto seg0 = centripetalCatmullRomToBezier(
            pMinus1, m_samples[0].point, m_samples[1].point, m_samples[2].point,
            m_samples[0].pressure, m_samples[1].pressure);
        result.newlyCommitted.push_back(seg0);
        m_committedCount = 1;
    }

    // When size >= 4, sliding window (Pi-1, Pi, Pi+1, Pi+2) commits span [Pi, Pi+1]
    while (m_committedCount + 2 < m_samples.size()) {
        const std::size_t i = m_committedCount;
        const auto seg = centripetalCatmullRomToBezier(
            m_samples[i - 1].point, m_samples[i].point, m_samples[i + 1].point,
            m_samples[i + 2].point, m_samples[i].pressure, m_samples[i + 1].pressure);
        result.newlyCommitted.push_back(seg);
        m_committedCount++;
    }

    return result;
}

std::vector<StrokeStabilizer::BezierSegment> StrokeStabilizer::endStroke() {
    std::vector<BezierSegment> finalSegments;
    const std::size_t n = m_samples.size();

    if (n < 2) {
        return finalSegments;
    }

    if (n == 2) {
        if (m_committedCount == 0) {
            const Point2D p0 = m_samples[0].point;
            const Point2D p1 = m_samples[1].point;
            finalSegments.push_back({p0, p0, p1, p1, m_samples[0].pressure, m_samples[1].pressure});
            m_committedCount = 1;
        }
        return finalSegments;
    }

    if (n == 3) {
        if (m_committedCount == 0) {
            const Point2D pMinus1 = {2.0 * m_samples[0].point.x - m_samples[1].point.x,
                                     2.0 * m_samples[0].point.y - m_samples[1].point.y};
            finalSegments.push_back(centripetalCatmullRomToBezier(
                pMinus1, m_samples[0].point, m_samples[1].point, m_samples[2].point,
                m_samples[0].pressure, m_samples[1].pressure));
            m_committedCount = 1;
        }
        if (m_committedCount == 1) {
            const Point2D pPlus1 = {2.0 * m_samples[2].point.x - m_samples[1].point.x,
                                    2.0 * m_samples[2].point.y - m_samples[1].point.y};
            finalSegments.push_back(centripetalCatmullRomToBezier(
                m_samples[0].point, m_samples[1].point, m_samples[2].point, pPlus1,
                m_samples[1].pressure, m_samples[2].pressure));
            m_committedCount = 2;
        }
        return finalSegments;
    }

    // Flush any pending middle spans
    while (m_committedCount + 2 < n) {
        const std::size_t i = m_committedCount;
        finalSegments.push_back(centripetalCatmullRomToBezier(
            m_samples[i - 1].point, m_samples[i].point, m_samples[i + 1].point,
            m_samples[i + 2].point, m_samples[i].pressure, m_samples[i + 1].pressure));
        m_committedCount++;
    }

    // Flush final tail segment [Pn-2, Pn-1] using synthesized lookahead
    if (m_committedCount < n - 1) {
        const Point2D pLast = {2.0 * m_samples[n - 1].point.x - m_samples[n - 2].point.x,
                               2.0 * m_samples[n - 1].point.y - m_samples[n - 2].point.y};
        finalSegments.push_back(centripetalCatmullRomToBezier(
            m_samples[n - 3].point, m_samples[n - 2].point, m_samples[n - 1].point, pLast,
            m_samples[n - 2].pressure, m_samples[n - 1].pressure));
        m_committedCount = n - 1;
    }

    return finalSegments;
}

StrokeStabilizer::BezierSegment
StrokeStabilizer::centripetalCatmullRomToBezier(Point2D p0, Point2D p1, Point2D p2, Point2D p3,
                                                double pr1, double pr2) {
    const double d01 = std::hypot(p1.x - p0.x, p1.y - p0.y);
    const double d12 = std::hypot(p2.x - p1.x, p2.y - p1.y);
    const double d23 = std::hypot(p3.x - p2.x, p3.y - p2.y);

    // Guard for coincident points: fallback to linear segment
    if (d12 < 1e-6) {
        return {p1, p1, p2, p2, pr1, pr2};
    }

    const double dt0 = std::sqrt(std::max(1e-4, d01));
    const double dt1 = std::sqrt(std::max(1e-4, d12));
    const double dt2 = std::sqrt(std::max(1e-4, d23));

    const double t0 = 0.0;
    const double t1 = t0 + dt0;
    const double t2 = t1 + dt1;
    const double t3 = t2 + dt2;

    // Centripetal tangent evaluation at P1 and P2
    const Point2D t1Vec = {(p1.x - p0.x) / dt0 - (p2.x - p0.x) / (t2 - t0) + (p2.x - p1.x) / dt1,
                           (p1.y - p0.y) / dt0 - (p2.y - p0.y) / (t2 - t0) + (p2.y - p1.y) / dt1};

    const Point2D t2Vec = {(p2.x - p1.x) / dt1 - (p3.x - p1.x) / (t3 - t1) + (p3.x - p2.x) / dt2,
                           (p2.y - p1.y) / dt1 - (p3.y - p1.y) / (t3 - t1) + (p3.y - p2.y) / dt2};

    // Cubic Bezier control points for span [P1, P2]
    const Point2D b0 = p1;
    const Point2D b1 = {p1.x + (dt1 / 3.0) * t1Vec.x, p1.y + (dt1 / 3.0) * t1Vec.y};
    const Point2D b2 = {p2.x - (dt1 / 3.0) * t2Vec.x, p2.y - (dt1 / 3.0) * t2Vec.y};
    const Point2D b3 = p2;

    return {b0, b1, b2, b3, pr1, pr2};
}

} // namespace FluidCoreApp
