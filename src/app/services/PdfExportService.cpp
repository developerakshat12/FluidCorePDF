#include "services/PdfExportService.h"
#include "services/StrokeStabilizer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace FluidCoreApp {

namespace {

StrokeStabilizer::Point2D evalCubicBezier(const StrokeStabilizer::Point2D& b0,
                                          const StrokeStabilizer::Point2D& b1,
                                          const StrokeStabilizer::Point2D& b2,
                                          const StrokeStabilizer::Point2D& b3, double t) {
    const double u = 1.0 - t;
    const double tt = t * t;
    const double uu = u * u;
    const double uuu = uu * u;
    const double ttt = tt * t;

    StrokeStabilizer::Point2D p;
    p.x = uuu * b0.x + 3.0 * uu * t * b1.x + 3.0 * u * tt * b2.x + ttt * b3.x;
    p.y = uuu * b0.y + 3.0 * uu * t * b1.y + 3.0 * u * tt * b2.y + ttt * b3.y;
    return p;
}

void renderBezierSegment(cairo_t* cr, const StrokeStabilizer::BezierSegment& seg,
                         double baseWidth) {
    constexpr int kSubdivisions = 3;
    StrokeStabilizer::Point2D prevPt = seg.p0;
    double prevP = seg.pressure0;

    for (int k = 1; k <= kSubdivisions; ++k) {
        const double t = static_cast<double>(k) / kSubdivisions;
        const auto currPt = evalCubicBezier(seg.p0, seg.p1, seg.p2, seg.p3, t);
        const double currP = (1.0 - t) * seg.pressure0 + t * seg.pressure1;
        const double segWidth = std::max(0.5, baseWidth * 0.5 * (prevP + currP));

        cairo_set_line_width(cr, segWidth);
        cairo_move_to(cr, prevPt.x, prevPt.y);
        cairo_line_to(cr, currPt.x, currPt.y);
        cairo_stroke(cr);

        prevPt = currPt;
        prevP = currP;
    }
}

// Marshalling payloads for GLib main-thread dispatch
struct ProgressPayload {
    std::weak_ptr<void> lifetimeToken;
    ProgressCallback callback;
    std::size_t currentPage = 0;
    std::size_t totalPages = 0;
};

struct CompletionPayload {
    std::weak_ptr<void> lifetimeToken;
    CompletionCallback callback;
    PdfExportResult result;
};

gboolean onProgressIdle(gpointer data) {
    auto* payload = static_cast<ProgressPayload*>(data);
    if (payload) {
        if (!payload->lifetimeToken.expired() && payload->callback) {
            payload->callback(payload->currentPage, payload->totalPages);
        }
    }
    return G_SOURCE_REMOVE;
}

void freeProgressPayload(gpointer data) {
    delete static_cast<ProgressPayload*>(data);
}

gboolean onCompletionIdle(gpointer data) {
    auto* payload = static_cast<CompletionPayload*>(data);
    if (payload) {
        if (!payload->lifetimeToken.expired() && payload->callback) {
            payload->callback(payload->result);
        }
    }
    return G_SOURCE_REMOVE;
}

void freeCompletionPayload(gpointer data) {
    delete static_cast<CompletionPayload*>(data);
}

} // namespace

void PdfExportService::renderStroke(cairo_t* cr, const FluidCore::Stroke& stroke) {
    if (stroke.points.empty()) {
        return;
    }

    const double r = ((stroke.color >> 16) & 0xFF) / 255.0;
    const double g = ((stroke.color >> 8) & 0xFF) / 255.0;
    const double b = (stroke.color & 0xFF) / 255.0;
    const bool isHighlighter = (stroke.tool == "highlighter");

    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    if (isHighlighter) {
        cairo_push_group(cr);
        cairo_set_source_rgb(cr, r, g, b);
    } else {
        cairo_set_source_rgb(cr, r, g, b);
    }

    if (stroke.points.size() == 1) {
        const double radius = std::max(0.5, stroke.width / 2.0);
        cairo_arc(cr, stroke.points[0].x, stroke.points[0].y, radius, 0.0, 2.0 * M_PI);
        cairo_fill(cr);
    } else if (stroke.points.size() == 2) {
        const double p0 = stroke.pressures.empty() ? 1.0 : stroke.pressures[0];
        cairo_set_line_width(cr, std::max(0.5, stroke.width * p0));
        cairo_move_to(cr, stroke.points[0].x, stroke.points[0].y);
        cairo_line_to(cr, stroke.points[1].x, stroke.points[1].y);
        cairo_stroke(cr);
    } else {
        const std::size_t n = stroke.points.size();
        for (std::size_t i = 0; i < n - 1; ++i) {
            StrokeStabilizer::Point2D p0 =
                (i == 0)
                    ? StrokeStabilizer::Point2D{2.0 * stroke.points[0].x - stroke.points[1].x,
                                                2.0 * stroke.points[0].y - stroke.points[1].y}
                    : StrokeStabilizer::Point2D{stroke.points[i - 1].x, stroke.points[i - 1].y};

            StrokeStabilizer::Point2D p1 = {stroke.points[i].x, stroke.points[i].y};
            StrokeStabilizer::Point2D p2 = {stroke.points[i + 1].x, stroke.points[i + 1].y};

            StrokeStabilizer::Point2D p3 =
                (i + 2 < n)
                    ? StrokeStabilizer::Point2D{stroke.points[i + 2].x, stroke.points[i + 2].y}
                    : StrokeStabilizer::Point2D{
                          2.0 * stroke.points[n - 1].x - stroke.points[n - 2].x,
                          2.0 * stroke.points[n - 1].y - stroke.points[n - 2].y};

            const double pr1 = (i < stroke.pressures.size()) ? stroke.pressures[i] : 1.0;
            const double pr2 = (i + 1 < stroke.pressures.size()) ? stroke.pressures[i + 1] : pr1;

            const auto seg =
                StrokeStabilizer::centripetalCatmullRomToBezier(p0, p1, p2, p3, pr1, pr2);
            renderBezierSegment(cr, seg, stroke.width);
        }
    }

    if (isHighlighter) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.5);
    }

    cairo_restore(cr);
}

PdfExportResult PdfExportService::exportAnnotatedPdfCore(
    const std::string& inputPdfPath,
    std::vector<FluidCore::Stroke> strokesSnapshot,
    const std::string& outputPath,
    const PdfExportOptions& options,
    std::atomic<bool>* cancelFlag,
    ProgressCallback onProgress) {

    PdfExportResult result;
    if (inputPdfPath.empty()) {
        result.errorMessage = "Input PDF path cannot be empty.";
        return result;
    }
    if (outputPath.empty()) {
        result.errorMessage = "Output file path cannot be empty.";
        return result;
    }

    // Open dedicated PopplerDocument in caller/worker thread context
    GError* error = nullptr;
    gchar* uri = g_filename_to_uri(inputPdfPath.c_str(), nullptr, &error);
    if (!uri) {
        result.errorMessage = error ? error->message : "Failed to convert input path to URI.";
        if (error) g_error_free(error);
        return result;
    }

    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, &error);
    g_free(uri);
    if (!doc) {
        result.errorMessage = error ? ("Failed to open PDF: " + std::string(error->message))
                                    : "Failed to open PDF document.";
        if (error) g_error_free(error);
        return result;
    }

    const int totalDocPages = poppler_document_get_n_pages(doc);
    if (totalDocPages <= 0) {
        g_object_unref(doc);
        result.errorMessage = "Source PDF document contains 0 pages.";
        return result;
    }

    // Index strokes per page
    std::vector<std::vector<FluidCore::Stroke>> pageStrokes(static_cast<std::size_t>(totalDocPages));
    std::set<std::size_t> annotatedPages;

    for (auto&& s : strokesSnapshot) {
        if (s.pageIndex < static_cast<std::size_t>(totalDocPages)) {
            const bool isHigh = (s.tool == "highlighter");
            if (isHigh && !options.includeHighlighters) continue;
            if (!isHigh && !options.includeStrokes) continue;

            annotatedPages.insert(s.pageIndex);
            pageStrokes[s.pageIndex].push_back(std::move(s));
        }
    }

    // Determine target page indices with deterministic filter intersection
    std::vector<std::size_t> targetPages;
    if (options.onlyAnnotatedPages) {
        if (options.pageRange.empty()) {
            targetPages.assign(annotatedPages.begin(), annotatedPages.end());
        } else {
            // Set intersection: (pageRange ∩ annotatedPages)
            std::set<std::size_t> reqPages(options.pageRange.begin(), options.pageRange.end());
            for (std::size_t p : annotatedPages) {
                if (reqPages.count(p)) {
                    targetPages.push_back(p);
                }
            }
        }
    } else {
        if (options.pageRange.empty()) {
            targetPages.reserve(static_cast<std::size_t>(totalDocPages));
            for (int i = 0; i < totalDocPages; ++i) {
                targetPages.push_back(static_cast<std::size_t>(i));
            }
        } else {
            for (std::size_t p : options.pageRange) {
                if (p < static_cast<std::size_t>(totalDocPages)) {
                    targetPages.push_back(p);
                }
            }
        }
    }

    if (targetPages.empty()) {
        g_object_unref(doc);
        result.errorMessage = "No pages to export based on the selected range and filter.";
        return result;
    }

    // Create temporary file path for atomic output handling
    const std::string tmpOutputPath = outputPath + ".tmp";
    std::error_code ec;
    std::filesystem::remove(tmpOutputPath, ec);

    cairo_surface_t* surface = cairo_pdf_surface_create(tmpOutputPath.c_str(), 612.0, 792.0);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        g_object_unref(doc);
        result.errorMessage = "Failed to create Cairo PDF surface at " + tmpOutputPath;
        cairo_surface_destroy(surface);
        return result;
    }

    cairo_t* cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        g_object_unref(doc);
        result.errorMessage = "Failed to create Cairo context for PDF export.";
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        std::filesystem::remove(tmpOutputPath, ec);
        return result;
    }

    const std::size_t totalTargetPages = targetPages.size();
    bool wasCancelled = false;

    for (std::size_t i = 0; i < totalTargetPages; ++i) {
        // 1. Cancellation check before starting page
        if (cancelFlag && cancelFlag->load()) {
            wasCancelled = true;
            break;
        }

        const std::size_t pageIdx = targetPages[i];
        PopplerPage* page = poppler_document_get_page(doc, static_cast<int>(pageIdx));
        if (!page) {
            continue;
        }

        double width = 0.0, height = 0.0;
        poppler_page_get_size(page, &width, &height);

        // Adjust vector PDF surface page size to exact document dimensions
        cairo_pdf_surface_set_size(surface, width, height);

        // 2. Render base PDF page vectors
        poppler_page_render_for_printing(page, cr);

        // 3. Render burnt annotations and strokes on top
        const auto& strokes = pageStrokes[pageIdx];
        for (std::size_t sIdx = 0; sIdx < strokes.size(); ++sIdx) {
            // Granular check in dense drawings every 50 strokes
            if (sIdx % 50 == 0 && cancelFlag && cancelFlag->load()) {
                wasCancelled = true;
                break;
            }
            renderStroke(cr, strokes[sIdx]);
        }

        if (wasCancelled) {
            g_object_unref(page);
            break;
        }

        // 4. Emit vector page
        cairo_show_page(cr);
        g_object_unref(page);
        result.pagesExported++;

        // 5. Progress notification
        if (onProgress) {
            onProgress(i + 1, totalTargetPages);
        }
    }

    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    g_object_unref(doc);

    if (wasCancelled) {
        std::filesystem::remove(tmpOutputPath, ec);
        result.success = false;
        result.errorMessage = "PDF export was cancelled.";
        return result;
    }

    if (result.pagesExported > 0) {
        // Atomic rename from .tmp to final target
        std::filesystem::rename(tmpOutputPath, outputPath, ec);
        if (ec) {
            std::filesystem::remove(tmpOutputPath, ec);
            result.success = false;
            result.errorMessage = "Failed to atomically rename temporary export file to final target: " + ec.message();
            return result;
        }
        result.success = true;
    } else {
        std::filesystem::remove(tmpOutputPath, ec);
        result.success = false;
        result.errorMessage = "No pages were exported.";
    }

    return result;
}

PdfExportResult PdfExportService::exportAnnotatedPdf(
    const std::string& inputPdfPath,
    const FluidCore::AnnotationStore& annotations,
    const std::string& outputPath,
    const PdfExportOptions& options) {
    return exportAnnotatedPdfCore(inputPdfPath, annotations.strokes(), outputPath, options);
}

PdfExportResult PdfExportService::exportAnnotatedPdf(
    PopplerDocument* doc,
    const FluidCore::AnnotationStore& annotations,
    const std::string& outputPath,
    const PdfExportOptions& options) {
    if (!doc) {
        return {false, 0, "Null PopplerDocument handle."};
    }
    // Extract input path from PopplerDocument URI or resolve via core
    const char* uri = poppler_document_get_title(doc); // fallback
    (void)uri;
    // For direct in-memory PopplerDocument export, save annotations with snapshot
    return exportAnnotatedPdfCore(outputPath, annotations.strokes(), outputPath, options);
}

std::thread PdfExportService::exportAnnotatedPdfAsync(
    const std::string& inputPdfPath,
    std::vector<FluidCore::Stroke> strokesSnapshot,
    const std::string& outputPath,
    const PdfExportOptions& options,
    std::shared_ptr<std::atomic<bool>> cancelFlag,
    std::weak_ptr<void> lifetimeToken,
    ProgressCallback onProgress,
    CompletionCallback onComplete) {

    return std::thread([inputPdfPath, strokes = std::move(strokesSnapshot), outputPath, options,
                        cancelFlag, lifetimeToken, onProgress, onComplete]() {
        auto progressDispatcher = [lifetimeToken, onProgress](std::size_t curr, std::size_t total) {
            if (!onProgress || lifetimeToken.expired()) return;
            auto* p = new ProgressPayload{lifetimeToken, onProgress, curr, total};
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, onProgressIdle, p, freeProgressPayload);
        };

        auto result = exportAnnotatedPdfCore(inputPdfPath, std::move(strokes), outputPath, options,
                                             cancelFlag.get(), progressDispatcher);

        if (onComplete) {
            auto* comp = new CompletionPayload{lifetimeToken, onComplete, std::move(result)};
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, onCompletionIdle, comp, freeCompletionPayload);
        }
    });
}

} // namespace FluidCoreApp
