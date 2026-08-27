#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace FluidCore {

// Pure C++20 Command interface for undo/redo operations across libfluidcore (ADR-0001).
// Zero GUI, GDK, GTK, Cairo, or Poppler dependencies.
class Command {
  public:
    virtual ~Command() = default;

    virtual bool execute() = 0;
    virtual bool undo() = 0;
    virtual bool redo() { return execute(); }

    virtual std::string description() const = 0;
    virtual std::size_t estimatedSizeBytes() const { return sizeof(*this); }
};

// Groups multiple sub-commands into a single atomic transactional undo/redo action.
class CompoundCommand : public Command {
  public:
    explicit CompoundCommand(std::string description = "Compound Action")
        : m_description(std::move(description)) {}

    void addCommand(std::unique_ptr<Command> command) {
        if (command) {
            m_commands.push_back(std::move(command));
        }
    }

    bool empty() const { return m_commands.empty(); }
    std::size_t size() const { return m_commands.size(); }
    const std::vector<std::unique_ptr<Command>>& commands() const { return m_commands; }

    bool execute() override {
        std::size_t executedCount = 0;
        for (auto& cmd : m_commands) {
            if (!cmd->execute()) {
                // Roll back on failure
                for (std::size_t i = executedCount; i > 0; --i) {
                    m_commands[i - 1]->undo();
                }
                return false;
            }
            executedCount++;
        }
        return true;
    }

    bool undo() override {
        bool allSucceeded = true;
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
            if (!(*it)->undo()) {
                allSucceeded = false;
            }
        }
        return allSucceeded;
    }

    bool redo() override {
        bool allSucceeded = true;
        for (auto& cmd : m_commands) {
            if (!cmd->redo()) {
                allSucceeded = false;
            }
        }
        return allSucceeded;
    }

    std::string description() const override { return m_description; }

    std::size_t estimatedSizeBytes() const override {
        std::size_t total =
            sizeof(*this) + m_commands.capacity() * sizeof(std::unique_ptr<Command>);
        for (const auto& cmd : m_commands) {
            total += cmd->estimatedSizeBytes();
        }
        return total;
    }

  private:
    std::string m_description;
    std::vector<std::unique_ptr<Command>> m_commands;
};

} // namespace FluidCore
