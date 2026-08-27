#pragma once

#include "undo/Command.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace FluidCore {

// Bounded-capacity Undo/Redo command manager with FIFO capacity trimming,
// byte-budget guards, and UI change notification callbacks.
class UndoStack {
  public:
    static constexpr std::size_t kDefaultMaxDepth = 100;
    static constexpr std::size_t kDefaultMaxBytes = 64 * 1024 * 1024; // 64 MB

    explicit UndoStack(std::size_t maxDepth = kDefaultMaxDepth,
                       std::size_t maxBytes = kDefaultMaxBytes);
    ~UndoStack();

    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    UndoStack(UndoStack&&) noexcept;
    UndoStack& operator=(UndoStack&&) noexcept;

    // Executes the command and records it on the undo stack. Truncates the redo stack.
    bool pushAndExecute(std::unique_ptr<Command> command);

    // Records an already-executed command onto the undo stack.
    void push(std::unique_ptr<Command> command);

    // Reverses the top command on the undo stack and moves it to the redo stack.
    bool undo();

    // Re-applies the top command on the redo stack and moves it to the undo stack.
    bool redo();

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

    std::size_t undoCount() const { return m_undoStack.size(); }
    std::size_t redoCount() const { return m_redoStack.size(); }

    std::string undoDescription() const;
    std::string redoDescription() const;

    const Command* peekUndo() const {
        return m_undoStack.empty() ? nullptr : m_undoStack.back().get();
    }
    const Command* peekRedo() const {
        return m_redoStack.empty() ? nullptr : m_redoStack.back().get();
    }

    void clear();

    std::size_t maxDepth() const { return m_maxDepth; }
    void setMaxDepth(std::size_t maxDepth) { m_maxDepth = maxDepth; }

    std::size_t maxBytes() const { return m_maxBytes; }
    void setMaxBytes(std::size_t maxBytes) { m_maxBytes = maxBytes; }

    std::size_t estimatedSizeBytes() const { return m_currentBytes; }

    void setChangeListener(std::function<void()> listener) {
        m_changeListener = std::move(listener);
    }

  private:
    void notifyListener();
    void trimCapacity();

    std::size_t m_maxDepth;
    std::size_t m_maxBytes;
    std::size_t m_currentBytes = 0;

    std::deque<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;

    std::function<void()> m_changeListener;
};

} // namespace FluidCore
