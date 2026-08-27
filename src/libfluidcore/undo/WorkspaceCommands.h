#pragma once

#include "undo/Command.h"
#include "workspace/WorkspaceModel.h"

#include <string>

namespace FluidCore {

// Command representing moving a WorkspaceNode to a new (x, y) origin.
// Library-level groundwork for Milestone M3 spatial canvas interactions.
class MoveNodeCommand : public Command {
  public:
    MoveNodeCommand(WorkspaceModel& model, std::string nodeId, Point oldPos, Point newPos);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Move Node"; }
    std::size_t estimatedSizeBytes() const override;

    const std::string& nodeId() const { return m_nodeId; }
    Point oldPosition() const { return m_oldPos; }
    Point newPosition() const { return m_newPos; }

  private:
    WorkspaceModel& m_model;
    std::string m_nodeId;
    Point m_oldPos;
    Point m_newPos;
};

} // namespace FluidCore
