#pragma once

#include "FluidCoreAPI.h"
#include "storage/AnnotationStore.h"

#include <string>

namespace FluidCore {

class CanvasStrokeNode : public WorkspaceNode {
  public:
    explicit CanvasStrokeNode(FluidCore::Stroke stroke);
    ~CanvasStrokeNode() override = default;

    const std::string& id() const override { return m_stroke.id; }
    Rectangle bounds() const override { return m_bounds; }
    void setPosition(double x, double y) override;

    const FluidCore::Stroke& stroke() const { return m_stroke; }
    void setStroke(FluidCore::Stroke stroke) {
        m_stroke = std::move(stroke);
        computeBounds();
    }

    std::unique_ptr<WorkspaceNode> clone() const override;

  private:
    void computeBounds();

    FluidCore::Stroke m_stroke;
    Rectangle m_bounds;
};

} // namespace FluidCore
