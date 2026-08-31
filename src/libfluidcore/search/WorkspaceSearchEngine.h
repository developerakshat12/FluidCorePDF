#pragma once

#include "FluidCoreAPI.h"
#include "workspace/WorkspaceModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace FluidCore {

struct WorkspaceSearchOptions {
    bool caseSensitive = false;
    bool tagsOnly = false;
};

class WorkspaceSearchEngine {
  public:
    // Performs in-memory full-text and tag search across all nodes in the WorkspaceModel.
    // Recursively inspects CardStackNode hierarchies, ExcerptCardNode snippets, and tags.
    static std::vector<WorkspaceMatch> search(const WorkspaceModel& model,
                                              const std::string& query,
                                              const WorkspaceSearchOptions& options = {});

    // Utility: extracts inline #hashtags from text strings (e.g. "Analysis of #methodology and #P1").
    static std::vector<std::string> extractHashtags(const std::string& text);

    // Utility: converts a list of WorkspaceMatch to generic SearchResult for public API.
    static std::vector<SearchResult> toSearchResults(const std::vector<WorkspaceMatch>& matches);
};

} // namespace FluidCore
