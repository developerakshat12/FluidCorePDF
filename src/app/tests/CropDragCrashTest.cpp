#include "FluidCoreEngine.h"
#include "document/DocumentPane.h"
#include "document/InkOverlay.h"
#include "services/ExcerptTileCache.h"
#include "services/PdfDocumentService.h"
#include "workspace/CardLayoutEngine.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/ExcerptPayload.h"
#include "workspace/WorkspaceInteraction.h"
#include "workspace/WorkspaceRenderer.h"
#include "workspace/WorkspaceState.h"
#include "undo/WorkspaceCommands.h"

#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    gtk_init_check(&argc, &argv);
    std::cout << "Starting CropDragCrashTest...\n";

    const std::string testPdf = "/mnt/d/study material/FIN F414 - FRAM/FRAMTextBook.pdf";

    FluidCore::FluidCoreEngine engine("crop_test_project");
    FluidCoreApp::WorkspaceState state;

    FluidCoreApp::PdfDocumentService docService;
    docService.registerMainDocument(testPdf, nullptr, testPdf);
    docService.registerMainDocument("doc-primary.pdf", nullptr, testPdf);
    docService.registerMainDocument("FRAMTextBook.pdf", nullptr, testPdf);

    FluidCoreApp::ExcerptTileCache tileCache(docService, 128 * 1024 * 1024);

    std::cout << "1. Simulating ExcerptDropPayload from crop selection on FRAMTextBook.pdf...\n";
    FluidCore::ExcerptDropPayload payload;
    payload.sourceDocId = testPdf;
    payload.sourcePageNo = 0;
    payload.sourceNormalizedRect = {0.12, 0.15, 0.45, 0.35};
    payload.sourcePageWidth = 612.0;
    payload.sourcePageHeight = 792.0;
    payload.textSnippet = "";
    payload.isImageExcerpt = true;
    payload.color = {168, 85, 247, 255};

    std::cout << "2. Computing card dimensions...\n";
    const auto [cardW, cardH] = FluidCore::CardLayoutEngine::computeExcerptCardDimensions(payload);
    std::cout << "Card dimensions: " << cardW << " x " << cardH << "\n";

    FluidCore::Rectangle cardBounds{200.0, 150.0, cardW, cardH};
    auto card = std::make_unique<FluidCore::ExcerptCardNode>(
        "excerpt-crop-1", cardBounds, payload.sourceDocId, payload.sourcePageNo,
        payload.sourceNormalizedRect, payload.textSnippet, payload.isImageExcerpt,
        payload.color, 1000);

    std::cout << "3. Inserting node into WorkspaceModel via InsertNodeCommand...\n";
    FluidCore::InsertNodeCommand cmd(engine.workspaceModel(), std::move(card));
    bool execOk = cmd.execute();
    std::cout << "InsertNodeCommand execute result: " << execOk << "\n";

    std::cout << "4. Testing WorkspaceRenderer::draw...\n";
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1200, 800);
    cairo_t* cr = cairo_create(surface);

    FluidCoreApp::WorkspaceRenderer::draw(cr, state, engine, &tileCache, 1200, 800);
    std::cout << "Initial WorkspaceRenderer::draw completed!\n";

    std::cout << "5. Running GLib main context iterations to process background render...\n";
    for (int i = 0; i < 50; ++i) {
        g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "6. Redrawing WorkspaceRenderer::draw after async tile rasterization...\n";
    FluidCoreApp::WorkspaceRenderer::draw(cr, state, engine, &tileCache, 1200, 800);
    std::cout << "Second WorkspaceRenderer::draw completed!\n";

    std::cout << "7. Testing bundle repoint and subsequent crop from bundled document path...\n";
    std::string bundledPath = "/mnt/d/study material/FIN F414 - FRAM/Test/Test1.ltproj/documents/FRAMTextBook.pdf";
    docService.repointDocumentPath(testPdf, bundledPath);

    FluidCore::ExcerptDropPayload payload2;
    payload2.sourceDocId = bundledPath;
    payload2.sourcePageNo = 10;
    payload2.sourceNormalizedRect = FluidCore::Rectangle{0.096, 0.311, 0.791, 0.225};
    payload2.sourcePageWidth = 612.0;
    payload2.sourcePageHeight = 792.0;
    payload2.isImageExcerpt = true;
    payload2.color = {168, 85, 247, 255};

    auto card2 = std::make_unique<FluidCore::ExcerptCardNode>(
        "excerpt-crop-2", cardBounds, payload2.sourceDocId, payload2.sourcePageNo,
        payload2.sourceNormalizedRect, "", true, payload2.color, 2000);
    FluidCore::InsertNodeCommand cmd2(engine.workspaceModel(), std::move(card2));
    cmd2.execute();

    FluidCoreApp::WorkspaceRenderer::draw(cr, state, engine, &tileCache, 1200, 800);
    for (int i = 0; i < 50; ++i) {
        g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FluidCoreApp::WorkspaceRenderer::draw(cr, state, engine, &tileCache, 1200, 800);

    // Verify surface in tileCache is valid
    FluidCoreApp::CropCacheKey key2 = FluidCoreApp::CropCacheKey::fromNormalizedRect(
        bundledPath, 10, payload2.sourceNormalizedRect, FluidCoreApp::LodTier::Standard);
    auto renderedSurface = tileCache.get(key2);
    if (!renderedSurface) {
        renderedSurface = tileCache.getBestAvailableSurface(bundledPath, 10, payload2.sourceNormalizedRect);
    }
    std::cout << "Crop on bundled document path surface valid: " << (renderedSurface ? "YES" : "NO") << "\n";
    if (!renderedSurface) {
        std::cerr << "FAILED to render crop from bundled document path!\n";
        return 1;
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    std::cout << "All CropDragCrashTest steps completed successfully!\n";
    return 0;
}
