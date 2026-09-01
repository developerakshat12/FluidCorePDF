#include "storage/ProjectStore.h"
#include "graph/GraphTopology.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace FluidCore;

namespace {

void testBundleCreationAndMetadata() {
    std::cout << "[ProjectStoreTest] testBundleCreationAndMetadata...\n";
    const std::string testDir = "build/test_project_store_bundle.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    ProjectStore store("proj-test-123");
    store.setProjectTitle("Analysis Synthesis");

    std::string err;
    bool ok = store.openProject(testDir, &err);
    assert(ok && "openProject should succeed");
    assert(store.isOpen());
    assert(store.metadata().projectId == "proj-test-123");
    assert(store.metadata().title == "Analysis Synthesis");
    assert(store.metadata().schemaVersion == 1);

    assert(std::filesystem::exists(testDir + "/project.db"));
    assert(std::filesystem::exists(testDir + "/documents"));
    assert(std::filesystem::exists(testDir + "/assets/clips"));
    assert(std::filesystem::exists(testDir + "/assets/images"));
    assert(std::filesystem::exists(testDir + "/cache/thumbnails"));

    store.closeProject();
    assert(!store.isOpen());

    // Reopen and verify metadata persists
    ProjectStore store2;
    ok = store2.openProject(testDir, &err);
    assert(ok);
    assert(store2.metadata().projectId == "proj-test-123");
    assert(store2.metadata().title == "Analysis Synthesis");
    store2.closeProject();

    std::filesystem::remove_all(testDir, ec);
    std::cout << "  Passed!\n";
}

void testDocumentRegistryAndCleanup() {
    std::cout << "[ProjectStoreTest] testDocumentRegistryAndCleanup...\n";
    const std::string testDir = "build/test_doc_registry.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    ProjectStore store("proj-docs");
    std::string err;
    assert(store.openProject(testDir, &err));

    DocumentRecord doc1{"doc-alpha", "PaperA.pdf", "documents/doc-alpha.pdf", "sha256-aaa", 15,
                        102400,      1234567};
    DocumentRecord doc2{"doc-beta", "PaperB.pdf", "documents/doc-beta.pdf", "sha256-bbb", 30,
                        204800,     1234568};

    assert(store.registerDocument(doc1, &err));
    assert(store.registerDocument(doc2, &err));

    auto list = store.listDocuments();
    assert(list.size() == 2);

    auto d1 = store.getDocument("doc-alpha");
    assert(d1.has_value());
    assert(d1->filename == "PaperA.pdf");
    assert(d1->pageCount == 15);

    // Create a companion .xopp file to test cleanup on deletion
    std::ofstream dummyXopp(testDir + "/documents/doc-alpha.xopp");
    dummyXopp << "<xournal/>";
    dummyXopp.close();
    assert(std::filesystem::exists(testDir + "/documents/doc-alpha.xopp"));

    // Remove doc-alpha
    assert(store.removeDocument("doc-alpha", &err));
    assert(store.listDocuments().size() == 1);
    assert(!store.getDocument("doc-alpha").has_value());

    // Verify filesystem companion was cleaned up
    assert(!std::filesystem::exists(testDir + "/documents/doc-alpha.xopp"));

    store.closeProject();
    std::filesystem::remove_all(testDir, ec);
    std::cout << "  Passed!\n";
}

void testNodeAndGraphRehydration() {
    std::cout << "[ProjectStoreTest] testNodeAndGraphRehydration...\n";
    const std::string testDir = "build/test_rehydration.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    ProjectStore store("proj-rehydrate");
    assert(store.openProject(testDir));

    WorkspaceModel model("proj-rehydrate");
    GraphTopology graph;

    // Create excerpt cards
    auto card1 = std::make_unique<ExcerptCardNode>("card-1", Rectangle{100, 200, 300, 150}, "doc-1",
                                                   3, Rectangle{0.1, 0.2, 0.4, 0.2},
                                                   "Quantum coherence in optical lattices", false);
    card1->addTag("physics");
    card1->addTag("quantum");

    auto card2 = std::make_unique<ExcerptCardNode>(
        "card-2", Rectangle{500, 200, 300, 150}, "doc-2", 7, Rectangle{0.05, 0.1, 0.5, 0.3},
        "Superconducting qubits with high fidelity", false);
    card2->addTag("hardware");

    // Create stack
    auto stack = std::make_unique<CardStackNode>("stack-1", Rectangle{100, 600, 320, 240},
                                                 "Quantum Stack", false);
    stack->addTag("summary");
    auto stackChild = std::make_unique<ExcerptCardNode>("card-3", Rectangle{110, 640, 280, 120},
                                                        "doc-1", 4, Rectangle{0.2, 0.3, 0.3, 0.3},
                                                        "Lattice depth modulation snippet", false);
    stack->addChild(std::move(stackChild));

    model.insert(std::move(card1));
    model.insert(std::move(card2));
    model.insert(std::move(stack));

    // Create graph edges
    GraphEdge edge1;
    edge1.id = "edge-1-2";
    edge1.sourceNodeId = "card-1";
    edge1.targetNodeId = "card-2";
    edge1.direction = EdgeDirection::Forward;
    edge1.color = {255, 69, 0, 255};
    edge1.strokeWidth = 3.0;
    edge1.label = "proves";
    graph.addEdge(edge1);

    // Save
    std::string err;
    DocumentRecord docRecord{"doc-1", "Quantum.pdf", "documents/doc-1.pdf", "sha-1", 10,
                             50000,   1000};
    bool saved = store.saveProject(model, graph, {docRecord}, &err);
    assert(saved && "saveProject should succeed");

    store.closeProject();

    // Reopen and rehydrate
    ProjectStore loadStore;
    assert(loadStore.openProject(testDir, &err));

    WorkspaceModel loadedModel("proj-rehydrate");
    GraphTopology loadedGraph;
    std::vector<DocumentRecord> loadedDocs;
    assert(loadStore.rehydrate(loadedModel, loadedGraph, loadedDocs, &err));

    assert(loadedDocs.size() == 1);
    assert(loadedDocs[0].docId == "doc-1");

    // Check top level nodes
    assert(loadedModel.nodeCount() == 3);
    auto* loadedCard1 = dynamic_cast<ExcerptCardNode*>(loadedModel.find("card-1"));
    assert(loadedCard1 != nullptr);
    assert(loadedCard1->bounds().x == 100);
    assert(loadedCard1->bounds().y == 200);
    assert(loadedCard1->sourceDocId() == "doc-1");
    assert(loadedCard1->sourcePageNo() == 3);
    assert(loadedCard1->sourceNormalizedRect().x == 0.1);
    assert(loadedCard1->textSnippet() == "Quantum coherence in optical lattices");
    assert(loadedCard1->hasTag("physics"));
    assert(loadedCard1->hasTag("quantum"));

    auto* loadedStack = dynamic_cast<CardStackNode*>(loadedModel.find("stack-1"));
    assert(loadedStack != nullptr);
    assert(loadedStack->title() == "Quantum Stack");
    assert(loadedStack->childCount() == 1);
    assert(loadedStack->hasTag("summary"));

    auto* loadedChild = dynamic_cast<ExcerptCardNode*>(loadedStack->findChild("card-3"));
    assert(loadedChild != nullptr);
    assert(loadedChild->textSnippet() == "Lattice depth modulation snippet");

    // Check graph edges
    assert(loadedGraph.edgeCount() == 1);
    auto loadedEdge = loadedGraph.findEdge("edge-1-2");
    assert(loadedEdge.has_value());
    assert(loadedEdge->sourceNodeId == "card-1");
    assert(loadedEdge->targetNodeId == "card-2");
    assert(loadedEdge->label == "proves");
    assert(loadedEdge->color.r == 255);
    assert(loadedEdge->color.g == 69);
    assert(loadedEdge->strokeWidth == 3.0);

    // Check FTS search
    auto results = loadStore.executeSearch("coherence");
    assert(!results.empty());
    assert(results[0].entityId == "card-1");

    auto stackResults = loadStore.executeSearch("Quantum Stack");
    assert(!stackResults.empty());

    loadStore.closeProject();
    std::filesystem::remove_all(testDir, ec);
    std::cout << "  Passed!\n";
}

} // namespace

int main() {
    std::cout << "Running ProjectStoreTest...\n";
    testBundleCreationAndMetadata();
    testDocumentRegistryAndCleanup();
    testNodeAndGraphRehydration();
    std::cout << "All ProjectStoreTest cases passed successfully!\n";
    return 0;
}
