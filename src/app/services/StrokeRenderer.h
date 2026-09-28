#pragma once

#include "FluidCoreAPI.h"
#include "geometry/StrokeWidthModel.h"
#include "services/StrokeStabilizer.h"
#include "storage/AnnotationStore.h"

#include <cairo.h>

#include <string>
#include <vector>

namespace FluidCoreApp {

// The single Cairo entry point for ink geometry.
//
// Every renderer in the app goes through these functions: the document pane overlay, the
// infinite canvas, PDF export, and excerpt crop tiles. They previously each carried
// their own copy of the stroke rasterizer, and those copies disagreed on both the
// flattening density and the pressure-to-width equation, so the same stroke rendered
// differently in each place and exported narrower than it displayed.
//
// Strokes are drawn at constant width: the Centripetal Catmull-Rom spline is emitted as
// cairo_curve_to spans and stroked once. Cairo's own stroker subdivides adaptively, so
// the outline is smooth at every zoom level, and a single stroke is a single composite
// rather than a chain of independently stroked chords.
//
// The earlier implementation flattened every span into a fixed 3 or 4 chords and
// stroked each chord separately. A fixed chord count is resolution dependent, so ink
// looked smooth at 50% zoom and visibly polygonal at 200%, and the chained round caps at
// stepping widths produced a beaded outline.
//
// Width does not vary with pressure. A stroke is exactly the width the user selected.
// The pressure vector is still captured and persisted with the stroke, but it does not
// affect geometry.

// Builds the Centripetal Catmull-Rom span covering points[i]..points[i+1],
// reflecting a phantom point past each end so the curve is defined across the entire
// polyline rather than only between interior samples.
StrokeStabilizer::BezierSegment
buildCentripetalSpan(const std::vector<FluidCore::XoppPoint>& points, std::size_t i);

// True for tools with a fixed nib, which are rendered in the translucent highlighter
// pass rather than as solid ink.
bool isConstantWidthTool(const std::string& tool);

// Renders a stored Stroke at its selected width. Leaves source color and alpha
// untouched; callers own those.
void renderStrokeGeometry(cairo_t* cr, const FluidCore::Stroke& stroke);

// Constant-width spline through points, stroked once.
void strokeConstantCairo(cairo_t* cr, const std::vector<FluidCore::XoppPoint>& points,
                         double width);

// Live/wet path. The streaming stabilizer emits Bezier spans rather than raw samples,
// so this consumes spans directly.
void strokeBezierSpansCairo(cairo_t* cr, const std::vector<StrokeStabilizer::BezierSegment>& spans,
                            double baseWidth);

} // namespace FluidCoreApp
