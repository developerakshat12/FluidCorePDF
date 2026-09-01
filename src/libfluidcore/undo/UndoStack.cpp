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

void UndoStack::beginMacro(std::string description) {
    if (m_macroDepth == 0) {
        m_activeMacro = std::make_unique<CompoundCommand>(std::move(description));
    }
    m_macroDepth++;
}

void UndoStack::endMacro() {
    if (m_macroDepth == 0) {
        return;
    }
    m_macroDepth--;
    if (m_macroDepth == 0) {
        if (m_activeMacro && !m_activeMacro->empty()) {
            auto macro = std::move(m_activeMacro);
            m_activeMacro = nullptr;

            const std::size_t sz = macro->estimatedSizeBytes();
            m_currentBytes += sz;
            m_undoStack.push_back(std::move(macro));

            trimCapacity();
            notifyListener();
        } else {
            m_activeMacro = nullptr;
        }
    }
}

void UndoStack::abortMacro() {
    if (m_macroDepth > 0 && m_activeMacro) {
        m_activeMacro->undo();
        m_activeMacro = nullptr;
        m_macroDepth = 0;
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

    if (isRecordingMacro()) {
        if (m_activeMacro) {
            if (m_activeMacro->empty()) {
                m_redoStack.clear();
                notifyListener();
            }
            m_activeMacro->addCommand(std::move(command));
        }
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
    if (m_macroDepth > 0 || m_undoStack.empty()) {
        return false;
    }

    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    m_currentBytes -= cmd->estimatedSizeBytes();
    const bool ok = cmd->undo();
    m_redoStack.push_back(std::move(cmd));

    notifyListener();
    return ok;
}

bool UndoStack::redo() {
    if (m_macroDepth > 0 || m_redoStack.empty()) {
        return false;
    }

    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    m_currentBytes += cmd->estimatedSizeBytes();
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
    m_activeMacro = nullptr;
    m_macroDepth = 0;
    m_undoStack.clear();
    m_redoStack.clear();
    m_currentBytes = 0;
    notifyListener();
}

} // namespace FluidCore
