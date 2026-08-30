#pragma once

#include "undo/Command.h"
#include "workspace/WorkspaceModel.h"

#include <memory>
#include <string>

namespace FluidCore {

// Command representing moving a WorkspaceNode to a new (x, y) origin.
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

// Command representing inserting a WorkspaceNode into the spatial scene graph.
class InsertNodeCommand : public Command {
  public:
    InsertNodeCommand(WorkspaceModel& model, std::unique_ptr<WorkspaceNode> node);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Insert Node"; }
    std::size_t estimatedSizeBytes() const override;

    const std::string& nodeId() const { return m_nodeId; }

  private:
    WorkspaceModel& m_model;
    std::string m_nodeId;
    std::unique_ptr<WorkspaceNode> m_nodeTemplate;
};

// Command representing removing a WorkspaceNode from the spatial scene graph.
class RemoveNodeCommand : public Command {
  public:
    RemoveNodeCommand(WorkspaceModel& model, std::string nodeId);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Remove Node"; }
    std::size_t estimatedSizeBytes() const override;

    const std::string& nodeId() const { return m_nodeId; }

  private:
    WorkspaceModel& m_model;
    std::string m_nodeId;
    std::unique_ptr<WorkspaceNode> m_savedNode;
};

} // namespace FluidCore
