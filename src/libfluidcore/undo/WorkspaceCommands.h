#pragma once

#include "undo/Command.h"
#include "workspace/CardStackNode.h"
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

// Command representing merging two nodes into a CardStackNode.
class StackMergeCommand : public Command {
  public:
    StackMergeCommand(WorkspaceModel& model, std::string sourceNodeId, std::string targetNodeId,
                      std::string stackId = "");

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Merge Stack"; }
    std::size_t estimatedSizeBytes() const override;

    const std::string& stackId() const { return m_stackId; }

  private:
    WorkspaceModel& m_model;
    std::string m_sourceId;
    std::string m_targetId;
    std::string m_stackId;
    bool m_targetWasStack = false;
    std::unique_ptr<WorkspaceNode> m_savedSourceNode;
    std::unique_ptr<WorkspaceNode> m_savedTargetNode;
};

// Command representing extracting a child card from a CardStackNode.
class ExtractChildCommand : public Command {
  public:
    ExtractChildCommand(WorkspaceModel& model, std::string stackId, std::string childId,
                        Point dropPos);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Extract Child Card"; }
    std::size_t estimatedSizeBytes() const override;

  private:
    WorkspaceModel& m_model;
    std::string m_stackId;
    std::string m_childId;
    Point m_dropPos;
    bool m_stackWasDissolved = false;
    std::unique_ptr<WorkspaceNode> m_savedStack;
    std::unique_ptr<WorkspaceNode> m_savedChild;
};

// Command representing toggling or setting collapsed state of a CardStackNode.
class ToggleStackCollapseCommand : public Command {
  public:
    ToggleStackCollapseCommand(WorkspaceModel& model, std::string stackId, bool collapsed);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Toggle Stack Collapse"; }
    std::size_t estimatedSizeBytes() const override;

  private:
    WorkspaceModel& m_model;
    std::string m_stackId;
    bool m_newCollapsed = false;
    bool m_oldCollapsed = false;
};

} // namespace FluidCore
