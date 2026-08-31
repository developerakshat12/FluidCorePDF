#include "search/WorkspaceSearchEngine.h"
#include "FluidCoreEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace FluidCore;

void testTextSnippetMatching() {
    WorkspaceModel model("test-proj");

    auto card1 = std::make_unique<ExcerptCardNode>(
        "card-1", Rectangle{100.0, 100.0, 200.0, 100.0}, "doc1.pdf", 0,
        Rectangle{0.1, 0.1, 0.5, 0.2},
        "Spatial indexing with R*-tree enables sub-millisecond search latencies.");

    auto card2 = std::make_unique<ExcerptCardNode>(
        "card-2", Rectangle{400.0, 100.0, 200.0, 100.0}, "doc1.pdf", 1,
        Rectangle{0.1, 0.2, 0.5, 0.3},
        "Centripetal Catmull-Rom stabilizer ensures smooth wet curve feedback.");

    model.insert(std::move(card1));
    model.insert(std::move(card2));

    // Case-insensitive query "spatial"
    auto matches = WorkspaceSearchEngine::search(model, "spatial");
    assert(matches.size() == 1);
    assert(matches[0].nodeId == "card-1");
    assert(matches[0].topLevelNodeId == "card-1");
    assert(matches[0].target == MatchTarget::TextSnippet);
    assert(matches[0].matchOffset == 0);
    assert(matches[0].matchLength == 7);
    assert(matches[0].bounds.x == 100.0);

    // Case-insensitive query "CATMULL-ROM"
    auto matches2 = WorkspaceSearchEngine::search(model, "CATMULL-ROM");
    assert(matches2.size() == 1);
    assert(matches2[0].nodeId == "card-2");
    assert(matches2[0].topLevelNodeId == "card-2");

    // Case-sensitive query
    WorkspaceSearchOptions optCase;
    optCase.caseSensitive = true;
    auto matches3 = WorkspaceSearchEngine::search(model, "spatial", optCase);
    assert(matches3.empty()); // "Spatial" in snippet starts with capital 'S'

    auto matches4 = WorkspaceSearchEngine::search(model, "Spatial", optCase);
    assert(matches4.size() == 1);

    std::cout << "[PASS] testTextSnippetMatching\n";
}

void testExplicitTagsAndInlineHashtags() {
    WorkspaceModel model("test-proj");

    auto card1 = std::make_unique<ExcerptCardNode>(
        "card-1", Rectangle{100.0, 100.0, 200.0, 100.0}, "doc1.pdf", 0,
        Rectangle{0.1, 0.1, 0.5, 0.2}, "Analysis of neural interface electrode arrays.");
    card1->addTag("methodology");
    card1->addTag("P1");

    auto card2 = std::make_unique<ExcerptCardNode>(
        "card-2", Rectangle{400.0, 100.0, 200.0, 100.0}, "doc1.pdf", 1,
        Rectangle{0.1, 0.2, 0.5, 0.3},
        "Cross-study comparison: #synthesis and #key-evidence for thesis.");

    model.insert(std::move(card1));
    model.insert(std::move(card2));

    // Search by explicit tag with '#' prefix
    auto matches1 = WorkspaceSearchEngine::search(model, "#methodology");
    assert(matches1.size() == 1);
    assert(matches1[0].nodeId == "card-1");
    assert(matches1[0].target == MatchTarget::Tag);
    assert(matches1[0].snippet == "#methodology");

    // Search with 'tag:' prefix
    auto matches2 = WorkspaceSearchEngine::search(model, "tag:p1");
    assert(matches2.size() == 1);
    assert(matches2[0].nodeId == "card-1");
    assert(matches2[0].target == MatchTarget::Tag);

    // Search by inline hashtag from text
    auto matches3 = WorkspaceSearchEngine::search(model, "#key-evidence");
    assert(matches3.size() == 1);
    assert(matches3[0].nodeId == "card-2");
    assert(matches3[0].target == MatchTarget::Tag);

    // General search matches both
    auto matchesAll = WorkspaceSearchEngine::search(model, "neural");
    assert(matchesAll.size() == 1);
    assert(matchesAll[0].nodeId == "card-1");

    std::cout << "[PASS] testExplicitTagsAndInlineHashtags\n";
}

void testCardStackHierarchicalSearch() {
    WorkspaceModel model("test-proj");

    auto stack = std::make_unique<CardStackNode>("stack-1", Rectangle{50.0, 50.0, 300.0, 200.0},
                                                 "Neural Architecture Synthesis");
    stack->addTag("neuro-ai");

    auto childCard1 = std::make_unique<ExcerptCardNode>(
        "child-1", Rectangle{50.0, 86.0, 300.0, 100.0}, "doc1.pdf", 2,
        Rectangle{0.1, 0.1, 0.5, 0.2},
        "Transformer self-attention mechanisms in biological modeling.");

    auto childCard2 = std::make_unique<ExcerptCardNode>(
        "child-2", Rectangle{50.0, 122.0, 300.0, 100.0}, "doc2.pdf", 5,
        Rectangle{0.2, 0.3, 0.6, 0.4}, "Spike timing dependent plasticity algorithms.");

    stack->addChild(std::move(childCard1));
    stack->addChild(std::move(childCard2));

    model.insert(std::move(stack));

    // Match stack title
    auto matchesTitle = WorkspaceSearchEngine::search(model, "Synthesis");
    assert(matchesTitle.size() == 1);
    assert(matchesTitle[0].nodeId == "stack-1");
    assert(matchesTitle[0].topLevelNodeId == "stack-1");
    assert(matchesTitle[0].target == MatchTarget::Title);

    // Match child card inside stack
    auto matchesChild = WorkspaceSearchEngine::search(model, "Transformer");
    assert(matchesChild.size() == 1);
    assert(matchesChild[0].nodeId == "child-1");
    assert(matchesChild[0].topLevelNodeId == "stack-1"); // Root ID is stack for camera centering
    assert(matchesChild[0].target == MatchTarget::TextSnippet);

    // Match second child inside stack
    auto matchesChild2 = WorkspaceSearchEngine::search(model, "plasticity");
    assert(matchesChild2.size() == 1);
    assert(matchesChild2[0].nodeId == "child-2");
    assert(matchesChild2[0].topLevelNodeId == "stack-1");

    // Match stack tag
    auto matchesTag = WorkspaceSearchEngine::search(model, "#neuro-ai");
    assert(matchesTag.size() == 1);
    assert(matchesTag[0].nodeId == "stack-1");
    assert(matchesTag[0].target == MatchTarget::Tag);

    std::cout << "[PASS] testCardStackHierarchicalSearch\n";
}

void testFluidCoreEngineIntegration() {
    FluidCoreEngine engine("engine-proj");

    auto card = std::make_unique<ExcerptCardNode>(
        "card-a", Rectangle{200.0, 200.0, 250.0, 120.0}, "paper.pdf", 0,
        Rectangle{0.0, 0.0, 1.0, 0.5}, "Infinite canvas coordinate transforms and camera gliding.");
    engine.insertNode(std::move(card));

    // Typed workspace search
    auto matches = engine.searchWorkspace("coordinate");
    assert(matches.size() == 1);
    assert(matches[0].nodeId == "card-a");

    // Generic SearchResult API
    auto results = engine.executeSearch("coordinate");
    assert(results.size() == 1);
    assert(results[0].entityId == "card-a");
    assert(results[0].entityType == "excerpt");
    assert(results[0].snippet.find("coordinate") != std::string::npos);

    // Empty query returns 0
    assert(engine.searchWorkspace("").empty());
    assert(engine.executeSearch("").empty());

    std::cout << "[PASS] testFluidCoreEngineIntegration\n";
}

int main() {
    std::cout << "Running WorkspaceSearchEngineTest...\n";
    testTextSnippetMatching();
    testExplicitTagsAndInlineHashtags();
    testCardStackHierarchicalSearch();
    testFluidCoreEngineIntegration();
    std::cout << "All WorkspaceSearchEngineTest cases passed!\n";
    return 0;
}
