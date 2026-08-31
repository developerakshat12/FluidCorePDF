#include "services/PdfExportService.h"
#include "storage/AnnotationStore.h"

#include <atomic>
#include <cassert>
#include <cairo-pdf.h>
#include <cairo.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <poppler.h>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace FluidCore;
using namespace FluidCoreApp;

namespace {

std::string createSyntheticPdf(const std::string& filePath, int numPages = 2) {
    cairo_surface_t* surface = cairo_pdf_surface_create(filePath.c_str(), 400.0, 600.0);
    cairo_t* cr = cairo_create(surface);

    for (int i = 0; i < numPages; ++i) {
        if (i % 2 == 1) {
            cairo_pdf_surface_set_size(surface, 500.0, 700.0);
        } else {
            cairo_pdf_surface_set_size(surface, 400.0, 600.0);
        }
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, 0.1 * (i + 1), 0.2, 0.3);
        cairo_rectangle(cr, 50, 50, 200, 100);
        cairo_fill(cr);
        cairo_show_page(cr);
    }

    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);

    return filePath;
}

void testPdfFlatteningWithStrokes() {
    const auto tempDir = std::filesystem::temp_directory_path();
    const std::string inPdfPath = (tempDir / "test_input_doc.pdf").string();
    const std::string outPdfPath = (tempDir / "test_annotated_output.pdf").string();

    createSyntheticPdf(inPdfPath, 2);
    assert(std::filesystem::exists(inPdfPath));

    std::vector<Stroke> strokes;

    // Add pen stroke on page 0
    Stroke penStroke;
    penStroke.id = "pen-01";
    penStroke.pageIndex = 0;
    penStroke.tool = "pen";
    penStroke.color = 0xFF0000; // Red
    penStroke.width = 3.0;
    penStroke.points = {{60.0, 60.0}, {100.0, 120.0}, {180.0, 140.0}, {250.0, 90.0}};
    penStroke.pressures = {1.0, 0.8, 0.6};
    strokes.push_back(penStroke);

    // Add highlighter stroke on page 1
    Stroke highStroke;
    highStroke.id = "high-01";
    highStroke.pageIndex = 1;
    highStroke.tool = "highlighter";
    highStroke.color = 0xFFFF00; // Yellow
    highStroke.width = 16.0;
    highStroke.points = {{100.0, 300.0}, {200.0, 300.0}, {350.0, 300.0}};
    highStroke.pressures = {1.0, 1.0};
    strokes.push_back(highStroke);

    auto result = PdfExportService::exportAnnotatedPdfCore(inPdfPath, strokes, outPdfPath);
    assert(result.success);
    assert(result.pagesExported == 2);
    assert(std::filesystem::exists(outPdfPath));

    // Verify output with Poppler
    GError* error = nullptr;
    gchar* uri = g_filename_to_uri(outPdfPath.c_str(), nullptr, &error);
    assert(uri != nullptr);

    PopplerDocument* verifyDoc = poppler_document_new_from_file(uri, nullptr, &error);
    g_free(uri);
    assert(verifyDoc != nullptr);

    int nPages = poppler_document_get_n_pages(verifyDoc);
    assert(nPages == 2);

    // Verify page dimensions preserved
    PopplerPage* p0 = poppler_document_get_page(verifyDoc, 0);
    double w0 = 0.0, h0 = 0.0;
    poppler_page_get_size(p0, &w0, &h0);
    assert(std::abs(w0 - 400.0) < 1.0);
    assert(std::abs(h0 - 600.0) < 1.0);
    g_object_unref(p0);

    PopplerPage* p1 = poppler_document_get_page(verifyDoc, 1);
    double w1 = 0.0, h1 = 0.0;
    poppler_page_get_size(p1, &w1, &h1);
    assert(std::abs(w1 - 500.0) < 1.0);
    assert(std::abs(h1 - 700.0) < 1.0);
    g_object_unref(p1);

    g_object_unref(verifyDoc);

    std::filesystem::remove(inPdfPath);
    std::filesystem::remove(outPdfPath);

    std::cout << "[PASS] testPdfFlatteningWithStrokes\n";
}

void testDeterministicCancellation() {
    const auto tempDir = std::filesystem::temp_directory_path();
    const std::string inPdfPath = (tempDir / "test_cancel_in.pdf").string();
    const std::string outPdfPath = (tempDir / "test_cancel_out.pdf").string();
    const std::string tmpPdfPath = outPdfPath + ".tmp";

    createSyntheticPdf(inPdfPath, 10); // 10 pages

    std::vector<Stroke> strokes;
    for (std::size_t i = 0; i < 10; ++i) {
        Stroke s;
        s.pageIndex = i;
        s.points = {{50.0, 50.0}, {100.0, 100.0}};
        strokes.push_back(s);
    }

    std::atomic<bool> cancelFlag{false};
    std::size_t progressCalls = 0;

    auto progressHook = [&](std::size_t curr, std::size_t total) {
        (void)total;
        progressCalls++;
        if (curr >= 4) {
            cancelFlag.store(true);
        }
    };

    auto result = PdfExportService::exportAnnotatedPdfCore(inPdfPath, strokes, outPdfPath, {},
                                                          &cancelFlag, progressHook);

    assert(!result.success);
    assert(result.errorMessage.find("cancelled") != std::string::npos);
    // Verify .tmp and final files do not exist
    assert(!std::filesystem::exists(outPdfPath));
    assert(!std::filesystem::exists(tmpPdfPath));

    std::filesystem::remove(inPdfPath);

    std::cout << "[PASS] testDeterministicCancellation\n";
}

void testOnlyAnnotatedPagesOption() {
    const auto tempDir = std::filesystem::temp_directory_path();
    const std::string inPdfPath = (tempDir / "test_annot_only_in.pdf").string();
    const std::string outPdfPath = (tempDir / "test_annot_only_out.pdf").string();

    createSyntheticPdf(inPdfPath, 6); // 6 pages total: 0, 1, 2, 3, 4, 5

    std::vector<Stroke> strokes;
    Stroke s1;
    s1.pageIndex = 1; // page 1
    s1.points = {{10.0, 10.0}, {20.0, 20.0}};
    strokes.push_back(s1);

    Stroke s2;
    s2.pageIndex = 4; // page 4
    s2.points = {{30.0, 30.0}, {40.0, 40.0}};
    strokes.push_back(s2);

    PdfExportOptions opts;
    opts.onlyAnnotatedPages = true;

    auto result = PdfExportService::exportAnnotatedPdfCore(inPdfPath, strokes, outPdfPath, opts);
    assert(result.success);
    assert(result.pagesExported == 2);

    // Verify output document has exactly 2 pages
    GError* error = nullptr;
    gchar* uri = g_filename_to_uri(outPdfPath.c_str(), nullptr, &error);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, &error);
    g_free(uri);
    assert(doc != nullptr);
    assert(poppler_document_get_n_pages(doc) == 2);
    g_object_unref(doc);

    std::filesystem::remove(inPdfPath);
    std::filesystem::remove(outPdfPath);

    std::cout << "[PASS] testOnlyAnnotatedPagesOption\n";
}

void testFilterIntersection() {
    const auto tempDir = std::filesystem::temp_directory_path();
    const std::string inPdfPath = (tempDir / "test_intersect_in.pdf").string();
    const std::string outPdfPath = (tempDir / "test_intersect_out.pdf").string();

    createSyntheticPdf(inPdfPath, 5); // pages 0, 1, 2, 3, 4

    std::vector<Stroke> strokes;
    Stroke s1;
    s1.pageIndex = 1;
    s1.points = {{10.0, 10.0}, {20.0, 20.0}};
    strokes.push_back(s1);

    Stroke s2;
    s2.pageIndex = 3;
    s2.points = {{10.0, 10.0}, {20.0, 20.0}};
    strokes.push_back(s2);

    // Request range [0, 1, 2] AND onlyAnnotatedPages. Annotated are {1, 3}.
    // Intersection must be exactly {1}.
    PdfExportOptions opts;
    opts.pageRange = {0, 1, 2};
    opts.onlyAnnotatedPages = true;

    auto result = PdfExportService::exportAnnotatedPdfCore(inPdfPath, strokes, outPdfPath, opts);
    assert(result.success);
    assert(result.pagesExported == 1);

    GError* error = nullptr;
    gchar* uri = g_filename_to_uri(outPdfPath.c_str(), nullptr, &error);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, &error);
    g_free(uri);
    assert(doc != nullptr);
    assert(poppler_document_get_n_pages(doc) == 1);
    g_object_unref(doc);

    std::filesystem::remove(inPdfPath);
    std::filesystem::remove(outPdfPath);

    std::cout << "[PASS] testFilterIntersection\n";
}

void testPopplerLoadFailureHandling() {
    std::vector<Stroke> strokes;
    auto result = PdfExportService::exportAnnotatedPdfCore(
        "nonexistent_file_path_12345.pdf", strokes, "output.pdf");

    assert(!result.success);
    assert(!result.errorMessage.empty());

    std::cout << "[PASS] testPopplerLoadFailureHandling\n";
}

} // namespace

int main() {
    std::cout << "Running PdfExportServiceTest suites...\n";
    testPdfFlatteningWithStrokes();
    testDeterministicCancellation();
    testOnlyAnnotatedPagesOption();
    testFilterIntersection();
    testPopplerLoadFailureHandling();
    std::cout << "All PdfExportServiceTest assertions PASSED (100%).\n";
    return 0;
}
