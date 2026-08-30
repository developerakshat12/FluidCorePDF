#pragma once

#include "squeeze/SqueezeEngine.h"
#include "undo/Command.h"

#include <cstddef>
#include <string>
#include <vector>

namespace FluidCore {

// Transactional command for creating, adjusting, or updating squeeze regions on a document.
class SetSqueezeRegionsCommand : public Command {
  public:
    SetSqueezeRegionsCommand(SqueezeEngine& engine, std::string docId,
                             std::vector<SqueezeRegion> newRegions);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Set Squeeze Fold"; }
    std::size_t estimatedSizeBytes() const override;

    const std::string& docId() const { return m_docId; }
    const std::vector<SqueezeRegion>& newRegions() const { return m_newRegions; }
    const std::vector<SqueezeRegion>& oldRegions() const { return m_oldRegions; }

  private:
    SqueezeEngine& m_engine;
    std::string m_docId;
    std::vector<SqueezeRegion> m_newRegions;
    std::vector<SqueezeRegion> m_oldRegions;
};

// Transactional command for resetting all squeeze regions on a document.
class ResetSqueezeCommand : public Command {
  public:
    ResetSqueezeCommand(SqueezeEngine& engine, std::string docId);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Reset Squeeze Folds"; }
    std::size_t estimatedSizeBytes() const override;

    const std::string& docId() const { return m_docId; }
    const std::vector<SqueezeRegion>& savedRegions() const { return m_savedRegions; }

  private:
    SqueezeEngine& m_engine;
    std::string m_docId;
    std::vector<SqueezeRegion> m_savedRegions;
};

} // namespace FluidCore
