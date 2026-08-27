#include "undo/UndoStack.h"

namespace FluidCore {

UndoStack::UndoStack(std::size_t maxDepth, std::size_t maxBytes)
    : m_maxDepth(maxDepth), m_maxBytes(maxBytes) {}

UndoStack::~UndoStack() {
    clear();
}

UndoStack::UndoStack(UndoStack&& other) noexcept
    : m_maxDepth(other.m_maxDepth), m_maxBytes(other.m_maxBytes),
      m_currentBytes(other.m_currentBytes), m_undoStack(std::move(other.m_undoStack)),
      m_redoStack(std::move(other.m_redoStack)),
      m_changeListener(std::move(other.m_changeListener)) {
    other.m_currentBytes = 0;
}

UndoStack& UndoStack::operator=(UndoStack&& other) noexcept {
    if (this != &other) {
        clear();
        m_maxDepth = other.m_maxDepth;
        m_maxBytes = other.m_maxBytes;
        m_currentBytes = other.m_currentBytes;
        m_undoStack = std::move(other.m_undoStack);
        m_redoStack = std::move(other.m_redoStack);
        m_changeListener = std::move(other.m_changeListener);
        other.m_currentBytes = 0;
    }
    return *this;
}

void UndoStack::notifyListener() {
    if (m_changeListener) {
        m_changeListener();
    }
}

void UndoStack::trimCapacity() {
    while ((m_undoStack.size() > m_maxDepth || m_currentBytes > m_maxBytes) &&
           m_undoStack.size() > 1) {
        m_currentBytes -= m_undoStack.front()->estimatedSizeBytes();
        m_undoStack.pop_front();
    }
}

bool UndoStack::pushAndExecute(std::unique_ptr<Command> command) {
    if (!command) {
        return false;
    }

    if (!command->execute()) {
        return false;
    }

    push(std::move(command));
    return true;
}

void UndoStack::push(std::unique_ptr<Command> command) {
    if (!command) {
        return;
    }

    // Truncate redo stack on any new mutation
    m_redoStack.clear();

    const std::size_t sz = command->estimatedSizeBytes();
    m_currentBytes += sz;
    m_undoStack.push_back(std::move(command));

    trimCapacity();
    notifyListener();
}

bool UndoStack::undo() {
    if (m_undoStack.empty()) {
        return false;
    }

    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    const bool ok = cmd->undo();
    m_redoStack.push_back(std::move(cmd));

    notifyListener();
    return ok;
}

bool UndoStack::redo() {
    if (m_redoStack.empty()) {
        return false;
    }

    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    const bool ok = cmd->redo();
    m_undoStack.push_back(std::move(cmd));

    trimCapacity();
    notifyListener();
    return ok;
}

std::string UndoStack::undoDescription() const {
    return canUndo() ? m_undoStack.back()->description() : "";
}

std::string UndoStack::redoDescription() const {
    return canRedo() ? m_redoStack.back()->description() : "";
}

void UndoStack::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
    m_currentBytes = 0;
    notifyListener();
}

} // namespace FluidCore
