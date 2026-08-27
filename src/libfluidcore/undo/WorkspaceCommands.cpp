#include "undo/WorkspaceCommands.h"

namespace FluidCore {

MoveNodeCommand::MoveNodeCommand(WorkspaceModel& model, std::string nodeId, Point oldPos,
                                 Point newPos)
    : m_model(model), m_nodeId(std::move(nodeId)), m_oldPos(oldPos), m_newPos(newPos) {}

bool MoveNodeCommand::execute() {
    return m_model.move(m_nodeId, m_newPos.x, m_newPos.y);
}

bool MoveNodeCommand::undo() {
    return m_model.move(m_nodeId, m_oldPos.x, m_oldPos.y);
}

bool MoveNodeCommand::redo() {
    return m_model.move(m_nodeId, m_newPos.x, m_newPos.y);
}

std::size_t MoveNodeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_nodeId.capacity();
}

} // namespace FluidCore
