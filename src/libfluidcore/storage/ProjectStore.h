#pragma once

#include "FluidCoreAPI.h"

namespace FluidCore {

// TODO(M5): .ltproj bundle over SQLite WAL + FTS5 (TRD §6). Stub-only until the
// schema-locking ADR lands; no DDL before docs/specs/ltspec.md (GOVERNANCE §4).
class ProjectStore {
  public:
    ProjectStore();
};

} // namespace FluidCore
