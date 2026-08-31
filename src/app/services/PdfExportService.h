#pragma once

#include "storage/AnnotationStore.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <cairo-pdf.h>
#include <cairo.h>
#include <poppler.h>

namespace FluidCoreApp {

struct PdfExportOptions {
    std::vector<std::size_t> pageRange; // Empty means all pages
    bool onlyAnnotatedPages = false;    // Fast compact export of pages with annotations
    bool includeStrokes = true;
    bool includeHighlighters = true;
};

struct PdfExportResult {
    bool success = false;
    std::size_t pagesExported = 0;
    std::string errorMessage;
};

using ProgressCallback = std::function<void(std::size_t currentPage, std::size_t totalPages)>;
using CompletionCallback = std::function<void(const PdfExportResult& result)>;

// High-fidelity vector PDF flattening service (M4 Step 4 / TASK-4.4).
// Renders native Poppler PDF pages and burns AnnotationStore pen/highlighter
// strokes into a standalone vector PDF via Cairo's PDF surface backend.
class PdfExportService {
  public:
    // Synchronous core export engine: processes page vectors and burns strokes.
    // Handles snapshot copying, filter intersection, granular cancellation, and atomic temp-swap.
    static PdfExportResult exportAnnotatedPdfCore(
        const std::string& inputPdfPath,
        std::vector<FluidCore::Stroke> strokesSnapshot,
        const std::string& outputPath,
        const PdfExportOptions& options = {},
        std::atomic<bool>* cancelFlag = nullptr,
        ProgressCallback onProgress = nullptr);

    // Synchronous convenience overloads for in-memory or static usage
    static PdfExportResult exportAnnotatedPdf(
        const std::string& inputPdfPath,
        const FluidCore::AnnotationStore& annotations,
        const std::string& outputPath,
        const PdfExportOptions& options = {});

    static PdfExportResult exportAnnotatedPdf(
        PopplerDocument* doc,
        const FluidCore::AnnotationStore& annotations,
        const std::string& outputPath,
        const PdfExportOptions& options = {});

    // Asynchronous RAII worker launcher: captures snapshot by value, spawns joinable worker thread,
    // and marshals progress and completion callbacks safely to the GLib main thread via g_idle_add_full.
    static std::thread exportAnnotatedPdfAsync(
        const std::string& inputPdfPath,
        std::vector<FluidCore::Stroke> strokesSnapshot,
        const std::string& outputPath,
        const PdfExportOptions& options,
        std::shared_ptr<std::atomic<bool>> cancelFlag,
        std::weak_ptr<void> lifetimeToken,
        ProgressCallback onProgress,
        CompletionCallback onComplete);

    // High-fidelity Cairo stroke renderer matching the Centripetal Catmull-Rom pipeline
    static void renderStroke(cairo_t* cr, const FluidCore::Stroke& stroke);
};

} // namespace FluidCoreApp
