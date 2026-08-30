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

    const FluidCore::Stroke& stroke() const { return m_stroke; }

    std::unique_ptr<WorkspaceNode> clone() const override;

  private:
    void computeBounds();

    FluidCore::Stroke m_stroke;
    Rectangle m_bounds;
};

} // namespace FluidCore
