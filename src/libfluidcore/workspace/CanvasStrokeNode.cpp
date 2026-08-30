#include "workspace/CanvasStrokeNode.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace FluidCore {

CanvasStrokeNode::CanvasStrokeNode(FluidCore::Stroke stroke) : m_stroke(std::move(stroke)) {
    computeBounds();
}

std::unique_ptr<WorkspaceNode> CanvasStrokeNode::clone() const {
    return std::make_unique<CanvasStrokeNode>(m_stroke);
}

void CanvasStrokeNode::computeBounds() {
    if (m_stroke.points.empty()) {
        m_bounds = Rectangle{0, 0, 0, 0};
        return;
    }

    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& pt : m_stroke.points) {
        if (pt.x < minX) minX = pt.x;
        if (pt.x > maxX) maxX = pt.x;
        if (pt.y < minY) minY = pt.y;
        if (pt.y > maxY) maxY = pt.y;
    }

    const double pad = m_stroke.width / 2.0 + 2.0;
    m_bounds = Rectangle{minX - pad, minY - pad, (maxX - minX) + 2.0 * pad, (maxY - minY) + 2.0 * pad};
}

} // namespace FluidCore
