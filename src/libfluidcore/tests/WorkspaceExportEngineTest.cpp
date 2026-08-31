#include "export/WorkspaceExportEngine.h"
#include "FluidCoreEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace FluidCore;

namespace {

// Minimal concrete node for testing unstacked free notes
class TestNoteNode final : public WorkspaceNode {
  public:
    TestNoteNode(std::string id, Rectangle bounds) : m_id(std::move(id)), m_bounds(bounds) {}
    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return m_bounds; }
    void setPosition(double x, double y) override {
        m_bounds.x = x;
        m_bounds.y = y;
    }

  private:
    std::string m_id;
    Rectangle m_bounds;
};

void testBasicMarkdownFormatting() {
    WorkspaceModel model("research-project");
    GraphTopology graph;

    auto card1 = std::make_unique<ExcerptCardNode>(
        "excerpt-1", Rectangle{100.0, 100.0, 200.0, 100.0}, "doc1.pdf", 0,
        Rectangle{0.1, 0.1, 0.5, 0.2}, "Spatial indexing enables sub-millisecond search latencies.",
        false);
    card1->addTag("indexing");
    card1->addTag("performance");

    auto card2 = std::make_unique<ExcerptCardNode>("crop-1", Rectangle{400.0, 100.0, 200.0, 100.0},
                                                   "doc1.pdf", 1, Rectangle{0.08, 0.12, 0.84, 0.35},
                                                   "", true);
    card2->addTag("diagram");

    model.insert(std::move(card1));
    model.insert(std::move(card2));

    auto result = WorkspaceExportEngine::exportToMarkdown(model, graph);
    assert(result.success);
    assert(result.totalCards == 2);
    assert(result.uniqueTagsCount == 3);

    // Verify Title & Metadata header
    assert(result.markdown.find("# Synthesis: research-project") != std::string::npos);
    assert(result.markdown.find("`research-project`") != std::string::npos);
    assert(result.markdown.find("`#indexing`") != std::string::npos);
    assert(result.markdown.find("`#diagram`") != std::string::npos);

    // Verify Excerpt blockquote and citation
    assert(result.markdown.find("> Spatial indexing enables sub-millisecond search latencies.") !=
           std::string::npos);
    assert(result.markdown.find("— *Source: `doc1.pdf` (Page 1)*") != std::string::npos);
    assert(result.markdown.find("· *Tags: `#indexing` `#performance`*") != std::string::npos);

    // Verify Visual Crop placeholder
    assert(result.markdown.find("> 🖼️ **[Visual Crop: `doc1.pdf`, Page 2]**") != std::string::npos);
    assert(result.markdown.find("Region: 8.0% x, 12.0% y, 84.0% w, 35.0% h") != std::string::npos);

    std::cout << "[PASS] testBasicMarkdownFormatting\n";
}

void testHierarchicalStackNesting() {
    WorkspaceModel model("nested-project");
    GraphTopology graph;

    auto rootStack = std::make_unique<CardStackNode>(
        "stack-root", Rectangle{50.0, 50.0, 400.0, 300.0}, "Machine Learning Foundations");
    rootStack->addTag("ml");

    auto cardInRoot = std::make_unique<ExcerptCardNode>(
        "card-root", Rectangle{60.0, 90.0, 200.0, 80.0}, "ml.pdf", 3, Rectangle{0.1, 0.1, 0.8, 0.2},
        "Deep learning models require balanced datasets.");

    auto childStack = std::make_unique<CardStackNode>(
        "stack-child", Rectangle{60.0, 180.0, 350.0, 150.0}, "Transformer Architectures");
    childStack->addTag("transformers");

    auto cardInChild = std::make_unique<ExcerptCardNode>(
        "card-child", Rectangle{70.0, 220.0, 200.0, 80.0}, "attention.pdf", 0,
        Rectangle{0.1, 0.2, 0.8, 0.3},
        "Attention mechanisms replace recurrence with self-attention.");

    childStack->addChild(std::move(cardInChild));
    rootStack->addChild(std::move(cardInRoot));
    rootStack->addChild(std::move(childStack));

    model.insert(std::move(rootStack));

    auto result = WorkspaceExportEngine::exportToMarkdown(model, graph);
    assert(result.success);
    assert(result.totalStacks == 2);
    assert(result.totalCards == 2);

    // Verify stack heading depth
    assert(result.markdown.find("### Machine Learning Foundations") != std::string::npos);
    assert(result.markdown.find("#### Transformer Architectures") != std::string::npos);
    assert(result.markdown.find("*Tags: `#ml`*") != std::string::npos);
    assert(result.markdown.find("*Tags: `#transformers`*") != std::string::npos);

    // Verify cards are included under stack
    assert(result.markdown.find("Deep learning models require balanced datasets.") !=
           std::string::npos);
    assert(result.markdown.find("Attention mechanisms replace recurrence with self-attention.") !=
           std::string::npos);

    std::cout << "[PASS] testHierarchicalStackNesting\n";
}

void testRelationalConnectionsAndMermaid() {
    WorkspaceModel model("graph-project");
    GraphTopology graph;

    auto cardA = std::make_unique<ExcerptCardNode>(
        "card-alpha", Rectangle{100.0, 100.0, 200.0, 80.0}, "docA.pdf", 0,
        Rectangle{0.1, 0.1, 0.8, 0.2}, "Premise: Hypothesis Alpha holds under standard pressure.");
    auto cardB = std::make_unique<ExcerptCardNode>(
        "card-beta", Rectangle{400.0, 100.0, 200.0, 80.0}, "docB.pdf", 2,
        Rectangle{0.1, 0.1, 0.8, 0.2}, "Conclusion: Empirical observations confirm Alpha.");

    model.insert(std::move(cardA));
    model.insert(std::move(cardB));

    // Register directed and bidirectional edges
    graph.addEdge("card-alpha", "card-beta", Color{30, 144, 255, 255}, 2.0,
                  ArrowStyle::SharpTriangle, "Supports");

    auto result = WorkspaceExportEngine::exportToMarkdown(model, graph);
    assert(result.success);
    assert(result.totalConnectors == 1);

    // Verify textual connection summary
    assert(result.markdown.find("## Relational Link Graph & Connections") != std::string::npos);
    assert(result.markdown.find("**`card-alpha`**") != std::string::npos);
    assert(result.markdown.find("──▶") != std::string::npos);
    assert(result.markdown.find("**`card-beta`**") != std::string::npos);
    assert(result.markdown.find("*[Supports]*") != std::string::npos);

    // Verify Mermaid block
    assert(result.markdown.find("```mermaid") != std::string::npos);
    assert(result.markdown.find("graph TD") != std::string::npos);
    assert(result.markdown.find("card_alpha") != std::string::npos);
    assert(result.markdown.find("card_beta") != std::string::npos);
    assert(result.markdown.find("-->|\"Supports\"|") != std::string::npos);

    std::cout << "[PASS] testRelationalConnectionsAndMermaid\n";
}

void testOptionTogglesAndFileExport() {
    WorkspaceModel model("options-project");
    GraphTopology graph;

    auto card = std::make_unique<ExcerptCardNode>("card-opt", Rectangle{50.0, 50.0, 200.0, 80.0},
                                                  "manual.pdf", 5, Rectangle{0.1, 0.1, 0.8, 0.2},
                                                  "Safety instructions for lab protocol.");
    model.insert(std::move(card));
    model.insert(
        std::make_unique<TestNoteNode>("note-generic", Rectangle{300.0, 50.0, 150.0, 80.0}));

    WorkspaceExportOptions opts;
    opts.customTitle = "Custom Synthesis Summary 2026";
    opts.includeMermaidGraph = false;
    opts.includeMetadataSummary = false;

    auto result = WorkspaceExportEngine::exportToMarkdown(model, graph, opts);
    assert(result.success);
    assert(result.markdown.find("# Custom Synthesis Summary 2026") != std::string::npos);
    assert(result.markdown.find("**Project**:") == std::string::npos); // Excluded metadata summary
    assert(result.markdown.find("```mermaid") == std::string::npos);   // Excluded mermaid graph
    assert(result.markdown.find("- **[note-generic]**") != std::string::npos); // Free generic note

    // Test file export
    const std::string tmpPath =
        (std::filesystem::temp_directory_path() / "test_workspace_export.md").string();
    std::string err;
    bool exported = WorkspaceExportEngine::exportToFile(tmpPath, model, graph, opts, &err);
    assert(exported);
    assert(std::filesystem::exists(tmpPath));

    std::ifstream in(tmpPath);
    std::string fileContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(fileContent == result.markdown);
    std::filesystem::remove(tmpPath);

    std::cout << "[PASS] testOptionTogglesAndFileExport\n";
}

void testFluidCoreEngineIntegration() {
    FluidCoreEngine engine("engine-project");

    engine.insertNode(std::make_unique<ExcerptCardNode>(
        "engine-card", Rectangle{10.0, 10.0, 200.0, 80.0}, "doc.pdf", 0,
        Rectangle{0.1, 0.1, 0.8, 0.2}, "Integrated engine export validation."));

    auto res = engine.exportWorkspaceMarkdown();
    assert(res.success);
    assert(res.markdown.find("Integrated engine export validation.") != std::string::npos);

    const std::string tmpPath =
        (std::filesystem::temp_directory_path() / "engine_export_test.md").string();
    assert(engine.exportWorkspaceMarkdownToFile(tmpPath));
    assert(std::filesystem::exists(tmpPath));
    std::filesystem::remove(tmpPath);

    std::cout << "[PASS] testFluidCoreEngineIntegration\n";
}

} // namespace

int main() {
    std::cout << "Running WorkspaceExportEngineTest suites...\n";
    testBasicMarkdownFormatting();
    testHierarchicalStackNesting();
    testRelationalConnectionsAndMermaid();
    testOptionTogglesAndFileExport();
    testFluidCoreEngineIntegration();
    std::cout << "All WorkspaceExportEngineTest assertions PASSED (100%).\n";
    return 0;
}
