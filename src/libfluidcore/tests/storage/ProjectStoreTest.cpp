#include "storage/ProjectStore.h"
#include "graph/GraphTopology.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sqlite3.h>

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
    bool ok = store.openProject(testDir);
    assert(ok && "store.openProject should succeed");

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
    bool opened = loadStore.openProject(testDir, &err);
    assert(opened);

    WorkspaceModel loadedModel("proj-rehydrate");
    GraphTopology loadedGraph;
    std::vector<DocumentRecord> loadedDocs;
    bool rehydrated = loadStore.rehydrate(loadedModel, loadedGraph, loadedDocs, &err);
    assert(rehydrated);

    assert(loadedDocs.size() == 1);
    assert(loadedDocs[0].docId == "doc-1");

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

void testIncrementalSavesAndPruning() {
    std::cout << "[ProjectStoreTest] testIncrementalSavesAndPruning...\n";
    const std::string testDir = "build/test_incremental_saves.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    ProjectStore store("proj-inc");
    std::string err;
    bool ok = store.openProject(testDir, &err);
    assert(ok);

    DocumentRecord doc;
    doc.docId = "doc-1";
    doc.filename = "doc1.pdf";
    doc.relativePath = "documents/doc1.pdf";
    doc.sha256 = "sha256-inc";
    doc.pageCount = 10;
    doc.fileSizeBytes = 1024;
    doc.createdAt = 1000;
    std::vector<DocumentRecord> docs = {doc};

    WorkspaceModel model("proj-inc");
    GraphTopology graph;

    // Add card A and card B, plus workspace stroke
    auto cardA =
        std::make_unique<ExcerptCardNode>("card-a", Rectangle{10, 20, 100, 50}, "doc-1", 1,
                                          Rectangle{0.1, 0.1, 0.2, 0.2}, "Original text A", false);
    model.insert(std::move(cardA));

    auto cardB =
        std::make_unique<ExcerptCardNode>("card-b", Rectangle{200, 200, 100, 50}, "doc-1", 2,
                                          Rectangle{0.1, 0.1, 0.2, 0.2}, "Original text B", false);
    model.insert(std::move(cardB));

    Stroke strokeData;
    strokeData.id = "stroke-inc-1";
    strokeData.tool = "pen";
    strokeData.color = 0x00FF00;
    strokeData.width = 2.0;
    strokeData.points = {{10.0, 20.0}, {25.0, 35.0}, {40.0, 50.0}};
    model.insert(std::make_unique<CanvasStrokeNode>(strokeData));

    GraphEdge edgeAB;
    edgeAB.id = "edge-ab";
    edgeAB.sourceNodeId = "card-a";
    edgeAB.targetNodeId = "card-b";
    edgeAB.direction = EdgeDirection::Forward;
    edgeAB.color = {0, 255, 0, 255};
    edgeAB.strokeWidth = 2.0;
    graph.addEdge(edgeAB);

    // Initial save
    assert(store.saveProject(model, graph, docs, &err));

    // Verify search
    auto resA = store.executeSearch("Original text A");
    assert(!resA.empty());
    assert(resA[0].entityId == "card-a");

    // Incremental update 1: Modify card A's position and text snippet, add card C
    auto* modA = dynamic_cast<ExcerptCardNode*>(model.find("card-a"));
    assert(modA);
    modA->setBounds(Rectangle{50, 60, 120, 70});
    modA->setTextSnippet("Updated text A modified");

    auto cardC = std::make_unique<ExcerptCardNode>("card-c", Rectangle{300, 300, 80, 40}, "doc-1",
                                                   3, Rectangle{0.1, 0.1, 0.2, 0.2},
                                                   "Newly added card C", false);
    model.insert(std::move(cardC));

    // Save incrementally
    assert(store.saveProject(model, graph, docs, &err));

    // Verify card A was updated in-place and card C was inserted
    auto resUpdatedA = store.executeSearch("Updated text A");
    assert(!resUpdatedA.empty());
    assert(resUpdatedA[0].entityId == "card-a");

    auto resOldA = store.executeSearch("Original text A");
    assert(resOldA.empty());

    auto resC = store.executeSearch("Newly added card C");
    assert(!resC.empty());
    assert(resC[0].entityId == "card-c");

    // Incremental update 2: Delete card B, stroke 1, and the edge
    graph.removeEdge("edge-ab");
    model.remove("card-b");
    model.remove("stroke-inc-1");

    // Save incrementally
    assert(store.saveProject(model, graph, docs, &err));

    // Close and reopen to verify rehydration matches
    store.closeProject();

    ProjectStore storeVerify;
    assert(storeVerify.openProject(testDir, &err));
    WorkspaceModel rehydratedModel("proj-inc");
    GraphTopology rehydratedGraph;
    std::vector<DocumentRecord> rehydratedDocs;
    assert(storeVerify.rehydrate(rehydratedModel, rehydratedGraph, rehydratedDocs, &err));

    assert(rehydratedModel.nodeCount() == 2);
    assert(rehydratedModel.find("card-b") == nullptr);
    assert(rehydratedModel.find("stroke-inc-1") == nullptr);

    auto* reA = dynamic_cast<ExcerptCardNode*>(rehydratedModel.find("card-a"));
    assert(reA);
    assert(reA->bounds().x == 50);
    assert(reA->bounds().y == 60);
    assert(reA->textSnippet() == "Updated text A modified");

    auto* reC = dynamic_cast<ExcerptCardNode*>(rehydratedModel.find("card-c"));
    assert(reC);
    assert(reC->bounds().x == 300);

    assert(rehydratedGraph.edgeCount() == 0);

    storeVerify.closeProject();
    std::filesystem::remove_all(testDir, ec);
    std::cout << "  Passed!\n";
}

void testUnrecognizedNodeTypeSafety() {
    std::cout << "[ProjectStoreTest] testUnrecognizedNodeTypeSafety...\n";
    const std::string testDir = "build/test_unrecognized_safety.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    ProjectStore store("proj-safety");
    std::string err;
    assert(store.openProject(testDir, &err));

    WorkspaceModel model("proj-safety");
    GraphTopology graph;
    std::vector<DocumentRecord> docs;

    // 1. Valid top-level excerpt card
    auto validCard = std::make_unique<ExcerptCardNode>("card-valid", Rectangle{10, 20, 100, 50},
                                                       "doc-1", 1, Rectangle{0.1, 0.1, 0.2, 0.2},
                                                       "Valid card snippet", false);
    model.insert(std::move(validCard));

    // 2. Valid stack with one valid child
    auto validStack = std::make_unique<CardStackNode>("stack-valid", Rectangle{300, 100, 250, 150},
                                                      "Valid Stack Header", false);
    auto stackChild = std::make_unique<ExcerptCardNode>(
        "card-stack-child-valid", Rectangle{310, 140, 200, 40}, "doc-1", 1,
        Rectangle{0.2, 0.2, 0.3, 0.3}, "Child snippet", false);
    validStack->addChild(std::move(stackChild));
    model.insert(std::move(validStack));

    // 3. Valid control ink stroke (2 points)
    Stroke controlStroke;
    controlStroke.id = "stroke-control-valid";
    controlStroke.tool = "pen";
    controlStroke.color = 0x0000FF;
    controlStroke.width = 3.0;
    controlStroke.points = {{100.0, 150.0}, {120.0, 180.0}};
    model.insert(std::make_unique<CanvasStrokeNode>(controlStroke));

    assert(store.saveProject(model, graph, docs, &err));
    store.closeProject();

    // Directly inject database anomalies:
    // Path A: Unrecognized top-level node + orphaned edge
    // Path B: Unrecognized child node attached to valid stack
    // Path C: Malformed ink_strokes rows (truncated bbox blob and misaligned points blob)
    sqlite3* db = nullptr;
    assert(sqlite3_open((testDir + "/project.db").c_str(), &db) == SQLITE_OK);

    const char* injectSql =
        // Path A: Unhandled root node (STICKY_NOTE is valid in DDL CHECK constraint, but not
        // handled in instantiateNode) & edge
        "INSERT INTO workspace_nodes (node_id, project_id, node_type, pos_x, pos_y, width, height, "
        "created_at, updated_at) "
        "VALUES ('corrupt-node-1', 'proj-safety', 'STICKY_NOTE', 100, 100, 200, 200, 1000, 1000);"
        "INSERT INTO graph_edges (edge_id, project_id, source_node_id, target_node_id, edge_type, "
        "edge_kind, direction, color, stroke_width, arrow_style, created_at) "
        "VALUES ('edge-to-corrupt', 'proj-safety', 'card-valid', 'corrupt-node-1', 'INK_LINK', "
        "'GENERIC', 0, 0, 1.0, 0, 1000);"
        // Path B: Unhandled child node inside stack-valid (TEXT_BOX is valid in DDL, but unhandled
        // in instantiateNode)
        "INSERT INTO workspace_nodes (node_id, project_id, node_type, pos_x, pos_y, width, height, "
        "parent_stack_id, created_at, updated_at) "
        "VALUES ('corrupt-child-1', 'proj-safety', 'TEXT_BOX', 310, 190, 200, 40, 'stack-valid', "
        "1000, 1000);"
        // Path C: Malformed ink_strokes rows
        // 1. Truncated bounding_box_blob (only 16 bytes instead of 32)
        "INSERT INTO ink_strokes (stroke_id, project_id, container_type, container_ref_id, "
        "page_index, bounding_box_blob, points_blob, tool_type, color, base_width, created_at) "
        "VALUES ('stroke-corrupt-bbox', 'proj-safety', 'WORKSPACE', 'proj-safety', NULL, "
        "zeroblob(16), zeroblob(32), 'pen', 255, 2.0, 1000);"
        // 2. Non-multiple of 16 points_blob (10 bytes)
        "INSERT INTO ink_strokes (stroke_id, project_id, container_type, container_ref_id, "
        "page_index, bounding_box_blob, points_blob, tool_type, color, base_width, created_at) "
        "VALUES ('stroke-corrupt-pts', 'proj-safety', 'WORKSPACE', 'proj-safety', NULL, "
        "zeroblob(32), zeroblob(10), 'pen', 255, 2.0, 1000);";

    char* sqlErr = nullptr;
    assert(sqlite3_exec(db, injectSql, nullptr, nullptr, &sqlErr) == SQLITE_OK);
    sqlite3_close(db);

    // Reopen and rehydrate
    ProjectStore verifyStore;
    assert(verifyStore.openProject(testDir, &err));
    WorkspaceModel reModel("proj-safety");
    GraphTopology reGraph;
    std::vector<DocumentRecord> reDocs;

    // Must not crash! Must succeed and gracefully skip corrupt entities while preserving valid
    // ones.
    assert(verifyStore.rehydrate(reModel, reGraph, reDocs, &err));

    // Verify Path A: corrupt top-level node skipped, orphaned edge discarded, valid card intact
    assert(reModel.find("corrupt-node-1") == nullptr);
    assert(reModel.find("card-valid") != nullptr);
    assert(reGraph.edgeCount() == 0);

    // Verify Path B: valid stack survived, corrupt child skipped, valid child retained without
    // nulls
    auto* reStack = dynamic_cast<CardStackNode*>(reModel.find("stack-valid"));
    assert(reStack != nullptr);
    assert(reStack->childCount() == 1);
    assert(reStack->findChild("card-stack-child-valid") != nullptr);
    assert(reStack->findChild("corrupt-child-1") == nullptr);

    // Verify Path C: corrupt stroke rows skipped, valid control stroke rehydrated intact
    assert(reModel.find("stroke-corrupt-bbox") == nullptr);
    assert(reModel.find("stroke-corrupt-pts") == nullptr);
    auto* reStroke = dynamic_cast<CanvasStrokeNode*>(reModel.find("stroke-control-valid"));
    assert(reStroke != nullptr);
    assert(reStroke->stroke().tool == "pen");
    assert(reStroke->stroke().points.size() == 2);

    verifyStore.closeProject();
    std::filesystem::remove_all(testDir, ec);
    std::cout << "  Passed!\n";
}

} // namespace

int main() {
    std::cout << "Running ProjectStoreTest...\n" << std::flush;
    testBundleCreationAndMetadata();
    testDocumentRegistryAndCleanup();
    testNodeAndGraphRehydration();
    testIncrementalSavesAndPruning();
    testUnrecognizedNodeTypeSafety();
    std::cout << "All ProjectStoreTest cases passed successfully!\n" << std::flush;
    return 0;
}
