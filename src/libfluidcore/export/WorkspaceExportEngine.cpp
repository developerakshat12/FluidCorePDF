#include "export/WorkspaceExportEngine.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace FluidCore {

namespace {

void collectTagsRecursive(const WorkspaceNode& node, std::set<std::string>& tags) {
    if (const auto* excerpt = dynamic_cast<const ExcerptCardNode*>(&node)) {
        for (const auto& tag : excerpt->tags()) {
            if (!tag.empty()) {
                tags.insert(tag);
            }
        }
    } else if (const auto* stack = dynamic_cast<const CardStackNode*>(&node)) {
        for (const auto& tag : stack->tags()) {
            if (!tag.empty()) {
                tags.insert(tag);
            }
        }
        for (const auto& child : stack->children()) {
            if (child) {
                collectTagsRecursive(*child, tags);
            }
        }
    }
}

std::string getExcerptTitleOrSnippet(const WorkspaceNode& node) {
    if (const auto* excerpt = dynamic_cast<const ExcerptCardNode*>(&node)) {
        if (!excerpt->textSnippet().empty()) {
            std::string snip = excerpt->textSnippet();
            if (snip.size() > 40) {
                snip = snip.substr(0, 37) + "...";
            }
            return snip;
        }
        if (excerpt->isImageExcerpt()) {
            return "Visual Crop (p. " + std::to_string(excerpt->sourcePageNo() + 1) + ")";
        }
        return "Excerpt Card";
    }
    if (const auto* stack = dynamic_cast<const CardStackNode*>(&node)) {
        return stack->title().empty() ? "Card Stack" : stack->title();
    }
    return node.id();
}

} // namespace

WorkspaceExportResult
WorkspaceExportEngine::exportToMarkdown(const WorkspaceModel& model, const GraphTopology& graph,
                                        const WorkspaceExportOptions& options) {
    WorkspaceExportResult result;
    std::ostringstream ss;

    // Collect all top-level node IDs
    const auto allNodeIds = model.allNodeIds();

    // Collect tags and count cards/stacks
    std::set<std::string> allTags;
    std::vector<const CardStackNode*> topStacks;
    std::vector<const WorkspaceNode*> freeNodes;

    for (const auto& id : allNodeIds) {
        const auto* node = model.find(id);
        if (!node) {
            continue;
        }
        collectTagsRecursive(*node, allTags);
        if (const auto* stack = dynamic_cast<const CardStackNode*>(node)) {
            topStacks.push_back(stack);
        } else {
            freeNodes.push_back(node);
        }
    }

    result.uniqueTagsCount = allTags.size();

    // 1. Header & Metadata Summary
    if (options.includeHeader) {
        const std::string title =
            options.customTitle.empty() ? "Synthesis: " + model.projectId() : options.customTitle;
        ss << "# " << title << "\n\n";

        if (options.includeMetadataSummary) {
            ss << "> **Project**: `" << model.projectId() << "`  \n";
            ss << "> **Total Nodes**: " << allNodeIds.size() << " (" << topStacks.size()
               << " stacks, " << freeNodes.size() << " free cards)  \n";
            ss << "> **Connections**: " << graph.edgeCount() << " relational links  \n";
            if (options.includeTags && !allTags.empty()) {
                ss << "> **Tags**: ";
                bool first = true;
                for (const auto& tag : allTags) {
                    if (!first)
                        ss << " ";
                    ss << "`#" << tag << "`";
                    first = false;
                }
                ss << "  \n";
            }
            ss << "\n---\n\n";
        }
    }

    // 2. Hierarchical Card Stacks
    if (!topStacks.empty()) {
        ss << "## Synthesis Clusters & Card Stacks\n\n";
        std::string formattedStacks;
        for (const auto* stack : topStacks) {
            formatStack(*stack, 2, options, formattedStacks, result.totalCards, result.totalStacks);
        }
        ss << formattedStacks;
    }

    // 3. Free Canvas Excerpts & Notes
    if (!freeNodes.empty()) {
        ss << "## Canvas Notes & Free Excerpts\n\n";
        for (const auto* node : freeNodes) {
            if (const auto* excerpt = dynamic_cast<const ExcerptCardNode*>(node)) {
                std::string cardStr;
                formatExcerptCard(*excerpt, 0, options, cardStr);
                ss << cardStr << "\n";
                result.totalCards++;
            } else {
                std::string nodeStr;
                formatGenericNode(*node, 0, options, nodeStr);
                ss << nodeStr << "\n";
            }
        }
    }

    // 4. Relational Connections Summary
    if (options.includeRelationalGraph && graph.edgeCount() > 0) {
        std::string graphStr;
        appendRelationalGraph(graph, model, options, graphStr, result.totalConnectors);
        ss << graphStr;
    }

    // 5. Mermaid Relationship Diagram
    if (options.includeMermaidGraph && graph.edgeCount() > 0) {
        std::string mermaidStr;
        appendMermaidGraph(graph, model, mermaidStr);
        ss << mermaidStr;
    }

    result.markdown = ss.str();
    result.success = true;
    return result;
}

bool WorkspaceExportEngine::exportToFile(const std::string& filePath, const WorkspaceModel& model,
                                         const GraphTopology& graph,
                                         const WorkspaceExportOptions& options,
                                         std::string* error) {
    const auto result = exportToMarkdown(model, graph, options);
    if (!result.success) {
        if (error) {
            *error = result.errorMessage.empty() ? "Markdown synthesis export failed"
                                                 : result.errorMessage;
        }
        return false;
    }

    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        if (error) {
            *error = "Could not open target file for writing: " + filePath;
        }
        return false;
    }

    out << result.markdown;
    if (out.fail()) {
        if (error) {
            *error = "Failed to write markdown content to file: " + filePath;
        }
        return false;
    }

    return true;
}

void WorkspaceExportEngine::formatStack(const CardStackNode& stack, std::size_t depth,
                                        const WorkspaceExportOptions& options, std::string& out,
                                        std::size_t& cardCount, std::size_t& stackCount) {
    stackCount++;
    std::size_t headingLevel = std::min<std::size_t>(depth + 1, 6);
    std::string heading(headingLevel, '#');

    const std::string title = stack.title().empty() ? "Untitled Stack" : stack.title();
    out += heading + " " + title + "\n\n";

    if (options.includeTags && !stack.tags().empty()) {
        out += "*Tags: ";
        for (std::size_t i = 0; i < stack.tags().size(); ++i) {
            if (i > 0)
                out += ", ";
            out += "`#" + stack.tags()[i] + "`";
        }
        out += "*\n\n";
    }

    for (const auto& child : stack.children()) {
        if (!child)
            continue;
        if (const auto* subStack = dynamic_cast<const CardStackNode*>(child.get())) {
            formatStack(*subStack, depth + 1, options, out, cardCount, stackCount);
        } else if (const auto* excerpt = dynamic_cast<const ExcerptCardNode*>(child.get())) {
            std::string cardOut;
            formatExcerptCard(*excerpt, 0, options, cardOut);
            out += cardOut + "\n";
            cardCount++;
        } else {
            std::string genOut;
            formatGenericNode(*child, 0, options, genOut);
            out += genOut + "\n";
        }
    }
    out += "\n";
}

void WorkspaceExportEngine::formatExcerptCard(const ExcerptCardNode& card,
                                              std::size_t /*indentLevel*/,
                                              const WorkspaceExportOptions& options,
                                              std::string& out) {
    if (card.isImageExcerpt()) {
        const auto& r = card.sourceNormalizedRect();
        std::ostringstream cropInfo;
        cropInfo << std::fixed << std::setprecision(1);
        cropInfo << "> 🖼️ **[Visual Crop: `" << card.sourceDocId() << "`, Page "
                 << (card.sourcePageNo() + 1) << "]**\n";
        cropInfo << "> *(Region: " << (r.x * 100.0) << "% x, " << (r.y * 100.0) << "% y, "
                 << (r.w * 100.0) << "% w, " << (r.h * 100.0) << "% h)*\n";

        if (options.includeSourceCitations) {
            cropInfo << ">\n> — *Source: `" << card.sourceDocId() << "` (Page "
                     << (card.sourcePageNo() + 1) << ")*";
            if (options.includeTags && !card.tags().empty()) {
                cropInfo << " · *Tags: ";
                for (std::size_t i = 0; i < card.tags().size(); ++i) {
                    if (i > 0)
                        cropInfo << " ";
                    cropInfo << "`#" << card.tags()[i] << "`";
                }
                cropInfo << "*";
            }
            cropInfo << "\n";
        }
        out += cropInfo.str();
        return;
    }

    std::string snippet = card.textSnippet().empty() ? "(Empty excerpt)" : card.textSnippet();
    // Split multiline snippets for clean blockquote formatting
    std::istringstream stream(snippet);
    std::string line;
    while (std::getline(stream, line)) {
        out += "> " + line + "\n";
    }

    if (options.includeSourceCitations) {
        out += ">\n> — *Source: `" + card.sourceDocId() + "` (Page " +
               std::to_string(card.sourcePageNo() + 1) + ")*";
        if (options.includeTags && !card.tags().empty()) {
            out += " · *Tags: ";
            for (std::size_t i = 0; i < card.tags().size(); ++i) {
                if (i > 0)
                    out += " ";
                out += "`#" + card.tags()[i] + "`";
            }
            out += "*";
        }
        out += "\n";
    }
}

void WorkspaceExportEngine::formatGenericNode(const WorkspaceNode& node,
                                              std::size_t /*indentLevel*/,
                                              const WorkspaceExportOptions& /*options*/,
                                              std::string& out) {
    const auto b = node.bounds();
    std::ostringstream oss;
    oss << "- **[" << node.id() << "]** *(Canvas pos: " << static_cast<int>(b.x) << ", "
        << static_cast<int>(b.y) << ")*\n";
    out += oss.str();
}

void WorkspaceExportEngine::appendRelationalGraph(const GraphTopology& graph,
                                                  const WorkspaceModel& model,
                                                  const WorkspaceExportOptions& /*options*/,
                                                  std::string& out, std::size_t& connectorCount) {
    const auto edgeIds = graph.allEdgeIds();
    if (edgeIds.empty()) {
        return;
    }

    out += "## Relational Link Graph & Connections\n\n";
    for (const auto& edgeId : edgeIds) {
        const auto edgeOpt = graph.findEdge(edgeId);
        if (!edgeOpt.has_value()) {
            continue;
        }
        const auto& edge = edgeOpt.value();
        connectorCount++;

        const auto* srcNode = model.findRecursive(edge.sourceNodeId);
        const auto* dstNode = model.findRecursive(edge.targetNodeId);

        const std::string srcLabel =
            srcNode ? getExcerptTitleOrSnippet(*srcNode) : edge.sourceNodeId;
        const std::string dstLabel =
            dstNode ? getExcerptTitleOrSnippet(*dstNode) : edge.targetNodeId;

        const std::string arrow =
            (edge.direction == EdgeDirection::Bidirectional) ? " ◀──▶ " : " ──▶ ";

        out += "* **`" + edge.sourceNodeId + "`** (" + srcLabel + ")" + arrow + "**`" +
               edge.targetNodeId + "`** (" + dstLabel + ")";
        if (!edge.label.empty()) {
            out += " *[" + edge.label + "]*";
        }
        out += "\n";
    }
    out += "\n";
}

void WorkspaceExportEngine::appendMermaidGraph(const GraphTopology& graph,
                                               const WorkspaceModel& model, std::string& out) {
    const auto edgeIds = graph.allEdgeIds();
    if (edgeIds.empty()) {
        return;
    }

    out += "```mermaid\ngraph TD\n";

    // Track declared nodes to avoid duplicate Mermaid declarations
    std::set<std::string> declaredNodes;

    for (const auto& edgeId : edgeIds) {
        const auto edgeOpt = graph.findEdge(edgeId);
        if (!edgeOpt.has_value()) {
            continue;
        }
        const auto& edge = edgeOpt.value();

        const auto* srcNode = model.findRecursive(edge.sourceNodeId);
        const auto* dstNode = model.findRecursive(edge.targetNodeId);

        const std::string srcId = sanitizeMermaidId(edge.sourceNodeId);
        const std::string dstId = sanitizeMermaidId(edge.targetNodeId);

        if (declaredNodes.find(srcId) == declaredNodes.end()) {
            const std::string label = srcNode
                                          ? sanitizeMermaidLabel(getExcerptTitleOrSnippet(*srcNode))
                                          : sanitizeMermaidLabel(edge.sourceNodeId);
            out += "    " + srcId + "[\"" + label + "\"]\n";
            declaredNodes.insert(srcId);
        }

        if (declaredNodes.find(dstId) == declaredNodes.end()) {
            const std::string label = dstNode
                                          ? sanitizeMermaidLabel(getExcerptTitleOrSnippet(*dstNode))
                                          : sanitizeMermaidLabel(edge.targetNodeId);
            out += "    " + dstId + "[\"" + label + "\"]\n";
            declaredNodes.insert(dstId);
        }

        if (edge.direction == EdgeDirection::Bidirectional) {
            if (!edge.label.empty()) {
                out += "    " + srcId + " <-->|\"" + sanitizeMermaidLabel(edge.label) + "\"| " +
                       dstId + "\n";
            } else {
                out += "    " + srcId + " <--> " + dstId + "\n";
            }
        } else {
            if (!edge.label.empty()) {
                out += "    " + srcId + " -->|\"" + sanitizeMermaidLabel(edge.label) + "\"| " +
                       dstId + "\n";
            } else {
                out += "    " + srcId + " --> " + dstId + "\n";
            }
        }
    }

    out += "```\n\n";
}

std::string WorkspaceExportEngine::sanitizeMermaidId(const std::string& id) {
    std::string clean = id;
    for (char& c : clean) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    if (clean.empty() || std::isdigit(static_cast<unsigned char>(clean[0]))) {
        clean = "node_" + clean;
    }
    return clean;
}

std::string WorkspaceExportEngine::sanitizeMermaidLabel(const std::string& label) {
    std::string clean;
    clean.reserve(label.size());
    for (char c : label) {
        if (c == '"' || c == '[' || c == ']' || c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '<' || c == '>' || c == '\n' || c == '\r') {
            clean += ' ';
        } else {
            clean += c;
        }
    }
    return clean;
}

} // namespace FluidCore
