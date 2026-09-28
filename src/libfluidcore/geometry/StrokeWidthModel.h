#pragma once

#include <algorithm>
#include <cmath>

namespace FluidCore {

// Canonical stroke width for all FluidCore ink rendering.
//
// Ink width is constant. Pressure is captured from the digitizer and persisted with
// each stroke, but it deliberately does not modulate geometry: a stroke is drawn at
// exactly the width the user selected, so a 2pt line stays a 2pt line across its whole
// length and a 4pt line stays 4pt.
//
// This was previously a taper, width(p) = baseWidth * (0.25 + 0.75 * p). It made the
// selected width mean different things at different points along the same stroke, and
// at the two places the effect was worst it read as a rendering fault rather than as
// style:
//
//   * On pen lift, the tail of a stroke collapsed toward the 0.25x floor while the round
//     end cap kept its full radius, producing a detached-looking blob at the end of
//     otherwise clean lines.
//   * The pressure vector persisted with a stroke, so the same saved stroke re-rendered
//     at a different apparent weight than the one the user drew, and the eraser sized
//     its hit radius from a pressure the ink no longer used.
//
// Every renderer (document pane, infinite canvas, PDF export, excerpt tiles) and the
// eraser hit-test derive width here, so on-screen ink and exported ink stay identical.

// Floor on rendered width, in page points, applied after clamping. Below roughly this,
// antialiasing degenerates into a dotted line.
inline constexpr double kMinStrokeWidthPx = 0.5;

// Uniform opacity for highlighter strokes, shared by the document pane, the infinite
// canvas, and PDF export so the same highlight reads identically in all three.
inline constexpr double kHighlighterAlpha = 0.5;

inline double clampPressure(double pressure) {
    if (!(pressure > 0.0)) { // also rejects NaN
        return 0.0;
    }
    return std::min(pressure, 1.0);
}

// Rendered width for any stroke, whatever the tool or the recorded pressure.
inline double renderedWidth(double baseWidth) {
    if (!(baseWidth > 0.0)) { // also rejects NaN
        return kMinStrokeWidthPx;
    }
    return std::max(kMinStrokeWidthPx, baseWidth);
}

// Half of renderedWidth, for callers that need a radius.
inline double renderedHalfWidth(double baseWidth) {
    return renderedWidth(baseWidth) * 0.5;
}

} // namespace FluidCore
