#pragma once

#include "RTreeIndex.h"

#include <string>

namespace FluidCore {

// TODO(M3): spatial scene graph over WorkspaceNode entities, TRD §3.4.
class WorkspaceModel {
  public:
    explicit WorkspaceModel(std::string projectId);
    const std::string& projectId() const { return m_projectId; }

  private:
    std::string m_projectId;
};

} // namespace FluidCore
