#include "undo/SqueezeCommands.h"

#include <utility>

namespace FluidCore {

SetSqueezeRegionsCommand::SetSqueezeRegionsCommand(SqueezeEngine& engine, std::string docId,
                                                   std::vector<SqueezeRegion> newRegions)
    : m_engine(engine), m_docId(std::move(docId)), m_newRegions(std::move(newRegions)) {
    if (m_engine.hasDocument(m_docId)) {
        m_oldRegions = m_engine.getRawRegions(m_docId);
    }
}

bool SetSqueezeRegionsCommand::execute() {
    m_engine.setRawRegions(m_docId, m_newRegions);
    return true;
}

bool SetSqueezeRegionsCommand::undo() {
    m_engine.setRawRegions(m_docId, m_oldRegions);
    return true;
}

bool SetSqueezeRegionsCommand::redo() {
    m_engine.setRawRegions(m_docId, m_newRegions);
    return true;
}

std::size_t SetSqueezeRegionsCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_docId.capacity() + m_newRegions.capacity() * sizeof(SqueezeRegion) +
           m_oldRegions.capacity() * sizeof(SqueezeRegion);
}

ResetSqueezeCommand::ResetSqueezeCommand(SqueezeEngine& engine, std::string docId)
    : m_engine(engine), m_docId(std::move(docId)) {
    if (m_engine.hasDocument(m_docId)) {
        m_savedRegions = m_engine.getRawRegions(m_docId);
    }
}

bool ResetSqueezeCommand::execute() {
    m_engine.resetSqueeze(m_docId);
    return true;
}

bool ResetSqueezeCommand::undo() {
    m_engine.setRawRegions(m_docId, m_savedRegions);
    return true;
}

bool ResetSqueezeCommand::redo() {
    m_engine.resetSqueeze(m_docId);
    return true;
}

std::size_t ResetSqueezeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_docId.capacity() + m_savedRegions.capacity() * sizeof(SqueezeRegion);
}

} // namespace FluidCore
