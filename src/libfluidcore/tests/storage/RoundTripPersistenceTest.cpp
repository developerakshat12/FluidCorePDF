#include "storage/AnnotationStore.h"
#include "storage/ProjectStore.h"
#include "storage/XoppDocument.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FluidCore;

namespace {

constexpr double kEpsilon = 1e-6;

bool floatNear(double a, double b, double eps = kEpsilon) {
    return std::abs(a - b) <= eps;
}

bool rectNear(const Rectangle& a, const Rectangle& b, double eps = kEpsilon) {
    return floatNear(a.x, b.x, eps) && floatNear(a.y, b.y, eps) && floatNear(a.w, b.w, eps) &&
           floatNear(a.h, b.h, eps);
}

void testFullRoundTripPersistenceWithRelocation() {
    std::cout << "[RoundTripPersistenceTest] Executing 6-Step Round-Trip Acceptance Test...\n";

    const std::string originalPath = "build/reference_project_original.ltproj";
    const std::string relocatedPath = "build/reference_project_relocated.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(originalPath, ec);
    std::filesystem::remove_all(relocatedPath, ec);

    // =========================================================================
    // STEP 1: Construct reference project with required complexity:
    // - >= 3 source PDFs in /documents/
    // - >= 20 excerpt cards (text and image crops) with non-trivial normalized_rect
    // - >= 10 ink connectors / edges between nodes
    // - Document companion .xopp annotation strokes
    // - Tags, stacks, and multi-node link chains
    // - Non-zero, non-default canvas coordinates
    // =========================================================================
    std::cout << "  Step 1: Constructing high-fidelity reference project...\n";

    ProjectStore originalStore("proj-acceptance-v1");
    originalStore.setProjectTitle("Full Round-Trip Acceptance Synthesis");
    assert(originalStore.openProject(originalPath));

    // Register 3 Documents
    std::vector<DocumentRecord> preDocs = {{"doc-1", "Deposition_A.pdf", "documents/doc-1.pdf",
                                            "sha256-depo-a", 45, 5242880, 1700000000},
                                           {"doc-2", "Contract_Spec.pdf", "documents/doc-2.pdf",
                                            "sha256-contract", 120, 12582912, 1700000100},
                                           {"doc-3", "Expert_Report.pdf", "documents/doc-3.pdf",
                                            "sha256-report", 80, 8388608, 1700000200}};

    for (const auto& doc : preDocs) {
        // Create dummy source PDF file
        std::ofstream pdfOut(originalPath + "/" + doc.relativePath);
        pdfOut << "%PDF-1.7 - mock content for " << doc.filename;
        pdfOut.close();
        assert(originalStore.registerDocument(doc));
    }

    // Create companion .xopp for doc-1
    AnnotationStore preAnnStore;
    Stroke stroke1;
    stroke1.id = "strk-001";
    stroke1.pageIndex = 2;
    stroke1.tool = "pen";
    stroke1.color = 0xFF0000;
    stroke1.width = 2.5;
    stroke1.points = {{100.25, 150.50}, {105.75, 155.00}, {115.00, 160.25}};
    preAnnStore.addStroke(2, stroke1);

    Stroke stroke2;
    stroke2.id = "strk-002";
    stroke2.pageIndex = 2;
    stroke2.tool = "highlighter";
    stroke2.color = 0xFFFF00;
    stroke2.width = 12.0;
    stroke2.points = {{200.0, 300.0}, {350.0, 300.0}};
    preAnnStore.addStroke(2, stroke2);

    assert(preAnnStore.saveAnnotations(originalPath + "/documents/doc-1.pdf"));

    // Build Workspace Model with 24 cards (18 text, 6 image crops), 2 stacks
    WorkspaceModel preModel("proj-acceptance-v1");

    std::vector<std::string> allCardIds;

    // 14 Top-level Cards
    for (int i = 1; i <= 14; ++i) {
        std::string cardId = "card-" + std::to_string(i);
        allCardIds.push_back(cardId);
        std::string docId = "doc-" + std::to_string((i % 3) + 1);
        size_t pageNo = static_cast<size_t>((i * 3) % 40 + 1);
        Rectangle normRect{0.105 + i * 0.03, 0.205 + i * 0.02, 0.355, 0.185};
        Rectangle canvasBounds{150.0 + (i * 45.0), 300.0 + (i * 65.0), 280.0, 140.0};
        std::string snippet =
            "Extracted textual clause #" + std::to_string(i) + " regarding patent liability terms.";
        bool isImage = (i % 4 == 0); // some image crops

        auto card = std::make_unique<ExcerptCardNode>(cardId, canvasBounds, docId, pageNo, normRect,
                                                      snippet, isImage);
        card->addTag("legal");
        if (i % 2 == 0)
            card->addTag("liability");
        if (i % 3 == 0)
            card->addTag("priority");
        preModel.insert(std::move(card));
    }

    // Stack 1 (with 5 nested cards)
    auto stack1 = std::make_unique<CardStackNode>(
        "stack-alpha", Rectangle{1200.0, 400.0, 320.0, 260.0}, "Damages Evidence", false);
    stack1->addTag("evidence");
    for (int j = 15; j <= 19; ++j) {
        std::string childId = "card-" + std::to_string(j);
        allCardIds.push_back(childId);
        Rectangle normRect{0.055 + j * 0.02, 0.155 + j * 0.03, 0.425, 0.225};
        Rectangle childBounds{1220.0, 440.0 + ((j - 15) * 30.0), 280.0, 120.0};
        std::string snippet =
            "Damages assessment paragraph #" + std::to_string(j) + " from financial report.";
        auto child = std::make_unique<ExcerptCardNode>(
            childId, childBounds, "doc-3", static_cast<size_t>(j), normRect, snippet, false);
        child->addTag("financials");
        stack1->addChild(std::move(child));
    }
    preModel.insert(std::move(stack1));

    // Stack 2 (with 5 nested cards, collapsed)
    auto stack2 = std::make_unique<CardStackNode>(
        "stack-beta", Rectangle{2000.0, 800.0, 320.0, 260.0}, "Technical Schematics", true);
    stack2->addTag("schematics");
    for (int k = 20; k <= 24; ++k) {
        std::string childId = "card-" + std::to_string(k);
        allCardIds.push_back(childId);
        Rectangle normRect{0.08 + k * 0.015, 0.12 + k * 0.015, 0.45, 0.25};
        Rectangle childBounds{2020.0, 840.0 + ((k - 20) * 30.0), 280.0, 120.0};
        std::string snippet = "Circuit diagram crop region #" + std::to_string(k);
        auto child = std::make_unique<ExcerptCardNode>(
            childId, childBounds, "doc-2", static_cast<size_t>(k), normRect, snippet, true);
        child->addTag("hardware");
        stack2->addChild(std::move(child));
    }
    preModel.insert(std::move(stack2));

    // Add freeform canvas ink strokes (CanvasStrokeNode)
    Stroke canvasPenStroke;
    canvasPenStroke.id = "canvas-stroke-pen-1";
    canvasPenStroke.tool = "pen";
    canvasPenStroke.color = 0x1E90FF;
    canvasPenStroke.width = 2.5;
    canvasPenStroke.points = {
        {500.125, 600.25}, {510.5, 620.75}, {525.0, 645.125}, {540.25, 670.0}, {555.75, 695.5}};
    preModel.insert(std::make_unique<CanvasStrokeNode>(canvasPenStroke));

    Stroke canvasHighlighterStroke;
    canvasHighlighterStroke.id = "canvas-stroke-hl-1";
    canvasHighlighterStroke.tool = "highlighter";
    canvasHighlighterStroke.color = 0xFFFF00;
    canvasHighlighterStroke.width = 16.0;
    canvasHighlighterStroke.points = {
        {600.0, 700.0}, {620.0, 705.0}, {640.0, 715.0}, {660.0, 720.0},
        {680.0, 730.0}, {700.0, 735.0}, {720.0, 745.0}, {740.0, 750.0}};
    preModel.insert(std::make_unique<CanvasStrokeNode>(canvasHighlighterStroke));

    // Build Graph Topology with 12 ink connectors/edges (including multi-node link chains)
    GraphTopology preGraph;
    for (int e = 1; e <= 12; ++e) {
        GraphEdge edge;
        edge.id = "edge-" + std::to_string(e);
        edge.sourceNodeId = allCardIds[e - 1];
        edge.targetNodeId = allCardIds[e];
        edge.direction = (e % 3 == 0) ? EdgeDirection::Bidirectional : EdgeDirection::Forward;
        edge.color = {static_cast<unsigned char>(e * 20), 100, 200, 255};
        edge.strokeWidth = 2.0 + (e * 0.25);
        edge.arrowStyle = (e % 2 == 0) ? ArrowStyle::SharpTriangle : ArrowStyle::OpenChevron;
        edge.label = "link_rel_" + std::to_string(e);
        preGraph.addEdge(edge);
    }

    // =========================================================================
    // STEP 2: Save via normal .ltproj commit path
    // =========================================================================
    std::cout << "  Step 2: Committing .ltproj bundle...\n";
    std::string err;
    assert(originalStore.saveProject(preModel, preGraph, preDocs, &err));

    // =========================================================================
    // STEP 3: Fully quit application (destroy all stores, models, and graphs)
    // =========================================================================
    std::cout << "  Step 3: Fully closing and releasing all memory handles...\n";
    originalStore.closeProject();

    // =========================================================================
    // STEP 4: Relocate .ltproj bundle to a different directory (Path Portability)
    // =========================================================================
    std::cout << "  Step 4: Relocating bundle to new path: " << relocatedPath << "\n";
    std::filesystem::rename(originalPath, relocatedPath, ec);
    assert(!ec && "Relocation must succeed");
    assert(!std::filesystem::exists(originalPath));
    assert(std::filesystem::exists(relocatedPath));

    // =========================================================================
    // STEP 5: Reopen the relocated .ltproj bundle
    // =========================================================================
    std::cout << "  Step 5: Reopening from relocated path...\n";
    ProjectStore reopenedStore;
    assert(reopenedStore.openProject(relocatedPath, &err));

    WorkspaceModel postModel("proj-acceptance-v1");
    GraphTopology postGraph;
    std::vector<DocumentRecord> postDocs;
    assert(reopenedStore.rehydrate(postModel, postGraph, postDocs, &err));

    // =========================================================================
    // STEP 6: Deep programmatic state comparison against pre-save snapshot
    // =========================================================================
    std::cout << "  Step 6: Executing deep state differential assertions...\n";

    // 1. Metadata verification
    assert(reopenedStore.metadata().projectId == "proj-acceptance-v1");
    assert(reopenedStore.metadata().title == "Full Round-Trip Acceptance Synthesis");
    assert(reopenedStore.metadata().schemaVersion == 1);

    // 2. Documents & path portability verification
    assert(postDocs.size() == 3);
    for (size_t d = 0; d < postDocs.size(); ++d) {
        assert(postDocs[d].docId == preDocs[d].docId);
        assert(postDocs[d].filename == preDocs[d].filename);
        assert(postDocs[d].relativePath == preDocs[d].relativePath);
        // Verify relative paths use forward slashes
        assert(postDocs[d].relativePath.find('\\') == std::string::npos);
        // Verify file actually exists at the relocated relative path
        assert(std::filesystem::exists(relocatedPath + "/" + postDocs[d].relativePath));
    }

    // 3. Top-level nodes count (14 root cards + 2 stacks + 2 freeform canvas strokes = 18 root nodes)
    assert(postModel.nodeCount() == 18);

    // 4. Excerpt card exact match assertions (all 24 cards)
    for (int i = 1; i <= 24; ++i) {
        std::string cardId = "card-" + std::to_string(i);
        WorkspaceNode* node = postModel.findRecursive(cardId);
        assert(node != nullptr && "Card must exist post-reload");

        auto* card = dynamic_cast<ExcerptCardNode*>(node);
        assert(card != nullptr);

        // A. doc_id, page_number match exactly
        if (i <= 14) {
            assert(card->sourceDocId() == "doc-" + std::to_string((i % 3) + 1));
            assert(card->sourcePageNo() == static_cast<size_t>((i * 3) % 40 + 1));
            Rectangle expectedNorm{0.105 + i * 0.03, 0.205 + i * 0.02, 0.355, 0.185};
            // B. normalized_rect matches with epsilon <= 1e-6
            assert(rectNear(card->sourceNormalizedRect(), expectedNorm));
            // C. Extracted text is byte-identical
            std::string expectedSnippet = "Extracted textual clause #" + std::to_string(i) +
                                          " regarding patent liability terms.";
            assert(card->textSnippet() == expectedSnippet);
            // D. Canvas position matches exactly
            Rectangle expectedBounds{150.0 + (i * 45.0), 300.0 + (i * 65.0), 280.0, 140.0};
            assert(rectNear(card->bounds(), expectedBounds));
            // E. Tags match
            assert(card->hasTag("legal"));
            if (i % 2 == 0)
                assert(card->hasTag("liability"));
            if (i % 3 == 0)
                assert(card->hasTag("priority"));
        } else if (i <= 19) {
            assert(card->sourceDocId() == "doc-3");
            assert(card->sourcePageNo() == static_cast<size_t>(i));
            Rectangle expectedNorm{0.055 + i * 0.02, 0.155 + i * 0.03, 0.425, 0.225};
            assert(rectNear(card->sourceNormalizedRect(), expectedNorm));
            std::string expectedSnippet =
                "Damages assessment paragraph #" + std::to_string(i) + " from financial report.";
            assert(card->textSnippet() == expectedSnippet);
            assert(card->hasTag("financials"));
        } else {
            assert(card->sourceDocId() == "doc-2");
            assert(card->sourcePageNo() == static_cast<size_t>(i));
            Rectangle expectedNorm{0.08 + i * 0.015, 0.12 + i * 0.015, 0.45, 0.25};
            assert(rectNear(card->sourceNormalizedRect(), expectedNorm));
            std::string expectedSnippet = "Circuit diagram crop region #" + std::to_string(i);
            assert(card->textSnippet() == expectedSnippet);
            assert(card->isImageExcerpt());
            assert(card->hasTag("hardware"));
        }
    }

    // 5. Stack hierarchy & collapse assertions
    auto* loadedStack1 = dynamic_cast<CardStackNode*>(postModel.find("stack-alpha"));
    assert(loadedStack1 != nullptr);
    assert(loadedStack1->title() == "Damages Evidence");
    assert(!loadedStack1->isCollapsed());
    assert(loadedStack1->childCount() == 5);
    assert(loadedStack1->hasTag("evidence"));

    auto* loadedStack2 = dynamic_cast<CardStackNode*>(postModel.find("stack-beta"));
    assert(loadedStack2 != nullptr);
    assert(loadedStack2->title() == "Technical Schematics");
    assert(loadedStack2->isCollapsed());
    assert(loadedStack2->childCount() == 5);
    assert(loadedStack2->hasTag("schematics"));

    // 6. Graph Edges & Zero dangling UUIDs assertions
    assert(postGraph.edgeCount() == 12);
    for (int e = 1; e <= 12; ++e) {
        std::string edgeId = "edge-" + std::to_string(e);
        auto edgeOpt = postGraph.findEdge(edgeId);
        assert(edgeOpt.has_value());

        const GraphEdge& edge = *edgeOpt;
        assert(edge.sourceNodeId == allCardIds[e - 1]);
        assert(edge.targetNodeId == allCardIds[e]);
        // Confirm endpoints actually exist in workspace
        assert(postModel.findRecursive(edge.sourceNodeId) != nullptr);
        assert(postModel.findRecursive(edge.targetNodeId) != nullptr);

        assert(edge.label == "link_rel_" + std::to_string(e));
        assert(floatNear(edge.strokeWidth, 2.0 + (e * 0.25)));
        if (e % 3 == 0) {
            assert(edge.direction == EdgeDirection::Bidirectional);
        } else {
            assert(edge.direction == EdgeDirection::Forward);
        }
    }

    // 7. Companion .xopp ink stroke preservation
    AnnotationStore postAnnStore;
    assert(postAnnStore.loadAnnotations(relocatedPath + "/documents/doc-1.pdf"));
    assert(postAnnStore.strokes().size() == 2);
    const auto& pStrk1 = postAnnStore.strokes()[0];
    assert(pStrk1.points.size() == 3);
    assert(floatNear(pStrk1.points[0].x, 100.25));
    assert(floatNear(pStrk1.points[0].y, 150.50));
    assert(pStrk1.color == 0xFF0000);

    const auto& pStrk2 = postAnnStore.strokes()[1];
    assert(pStrk2.tool == "highlighter");
    assert(pStrk2.color == 0xFFFF00);

    // 8. FTS Universal Search verification post-reload
    auto results = reopenedStore.executeSearch("patent liability");
    assert(!results.empty());

    auto damResults = reopenedStore.executeSearch("Damages Evidence");
    assert(!damResults.empty());

    // 9. Freeform canvas ink strokes (CanvasStrokeNode) verification post-reload
    auto* loadedPen = dynamic_cast<CanvasStrokeNode*>(postModel.find("canvas-stroke-pen-1"));
    assert(loadedPen != nullptr && "Canvas pen stroke must be rehydrated as CanvasStrokeNode");
    assert(loadedPen->stroke().tool == "pen");
    assert(loadedPen->stroke().color == 0x1E90FF);
    assert(floatNear(loadedPen->stroke().width, 2.5));
    assert(loadedPen->stroke().points.size() == 5);
    assert(floatNear(loadedPen->stroke().points[0].x, 500.125));
    assert(floatNear(loadedPen->stroke().points[0].y, 600.25));
    assert(floatNear(loadedPen->stroke().points[4].x, 555.75));
    assert(floatNear(loadedPen->stroke().points[4].y, 695.5));
    CanvasStrokeNode expectedPenNode(canvasPenStroke);
    assert(rectNear(loadedPen->bounds(), expectedPenNode.bounds()));

    auto* loadedHl = dynamic_cast<CanvasStrokeNode*>(postModel.find("canvas-stroke-hl-1"));
    assert(loadedHl != nullptr && "Canvas highlighter stroke must be rehydrated as CanvasStrokeNode");
    assert(loadedHl->stroke().tool == "highlighter");
    assert(loadedHl->stroke().color == 0xFFFF00);
    assert(floatNear(loadedHl->stroke().width, 16.0));
    assert(loadedHl->stroke().points.size() == 8);
    assert(floatNear(loadedHl->stroke().points[0].x, 600.0));
    assert(floatNear(loadedHl->stroke().points[7].x, 740.0));
    CanvasStrokeNode expectedHlNode(canvasHighlighterStroke);
    assert(rectNear(loadedHl->bounds(), expectedHlNode.bounds()));

    reopenedStore.closeProject();
    std::filesystem::remove_all(relocatedPath, ec);

    std::cout << "  ALL 10 ACCEPTANCE CRITERIA PASSED 100%!\n";
}

} // namespace

int main() {
    std::cout << "Running RoundTripPersistenceTest...\n";
    testFullRoundTripPersistenceWithRelocation();
    std::cout << "All RoundTripPersistenceTest cases passed successfully!\n";
    return 0;
}
