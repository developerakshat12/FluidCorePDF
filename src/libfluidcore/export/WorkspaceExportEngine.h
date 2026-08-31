#pragma once

#include "FluidCoreAPI.h"
#include "graph/GraphTopology.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace FluidCore {

// Pure C++20 exporter serializing the visual synthesis canvas, nested card stacks,
// excerpt cards, citations, tags, free notes, and relational graphs into standard Markdown.
class WorkspaceExportEngine {
  public:
    // Exports the workspace model and graph topology to a Markdown formatted string.
    static WorkspaceExportResult exportToMarkdown(const WorkspaceModel& model,
                                                  const GraphTopology& graph,
                                                  const WorkspaceExportOptions& options = {});

    // Exports the workspace model and graph topology directly to a file on disk.
    static bool exportToFile(const std::string& filePath, const WorkspaceModel& model,
                             const GraphTopology& graph, const WorkspaceExportOptions& options = {},
                             std::string* error = nullptr);

  private:
    static void formatStack(const CardStackNode& stack, std::size_t depth,
                            const WorkspaceExportOptions& options, std::string& out,
                            std::size_t& cardCount, std::size_t& stackCount);

    static void formatExcerptCard(const ExcerptCardNode& card, std::size_t indentLevel,
                                  const WorkspaceExportOptions& options, std::string& out);

    static void formatGenericNode(const WorkspaceNode& node, std::size_t indentLevel,
                                  const WorkspaceExportOptions& options, std::string& out);

    static void appendRelationalGraph(const GraphTopology& graph, const WorkspaceModel& model,
                                      const WorkspaceExportOptions& options, std::string& out,
                                      std::size_t& connectorCount);

    static void appendMermaidGraph(const GraphTopology& graph, const WorkspaceModel& model,
                                   std::string& out);

    static std::string sanitizeMermaidId(const std::string& id);
    static std::string sanitizeMermaidLabel(const std::string& label);
};

} // namespace FluidCore
