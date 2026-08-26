// Headless unit tests for AnnotationStore (.xopp persistence loop and stroke
// management).

#include "storage/AnnotationStore.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace FluidCore;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

std::filesystem::path tempFile(const char* suffix) {
    static int counter = 0;
    return std::filesystem::temp_directory_path() /
           ("fluidcore_ann_test_" + std::to_string(counter++) + "_" + std::to_string(std::rand()) +
            suffix);
}

int testStrokeManagement() {
    int failures = 0;
    AnnotationStore store;

    Stroke s1;
    s1.tool = "pen";
    s1.color = 0x112233;
    s1.width = 2.0;
    s1.pressures = {0.8, 1.2};
    s1.points = {{10.0, 20.0}, {30.0, 40.0}, {50.0, 60.0}};

    Stroke s2;
    s2.tool = "highlighter";
    s2.color = 0xFFFF00;
    s2.width = 10.0;
    s2.points = {{100.0, 100.0}, {200.0, 100.0}};

    const std::string id1 = store.addStroke(0, s1);
    const std::string id2 = store.addStroke(1, s2);

    failures += check(!id1.empty() && !id2.empty(), "addStroke generates valid non-empty IDs");
    failures += check(store.strokes().size() == 2, "store holds 2 strokes");
    failures += check(store.strokesForPage(0).size() == 1, "page 0 has 1 stroke");
    failures += check(store.strokesForPage(1).size() == 1, "page 1 has 1 stroke");
    failures += check(store.strokesForPage(2).empty(), "page 2 has 0 strokes");

    const Stroke* found = store.findStroke(id1);
    failures += check(found != nullptr && found->color == 0x112233, "findStroke locates by ID");

    failures += check(store.removeStroke(id1), "removeStroke succeeds for valid ID");
    failures += check(store.strokes().size() == 1, "store has 1 stroke after removal");
    failures += check(store.strokesForPage(0).empty(), "page 0 is now empty");
    failures += check(!store.removeStroke("nonexistent-id"), "removeStroke fails on invalid ID");

    store.clear();
    failures += check(store.strokes().empty(), "clear() resets stroke collection");

    return failures;
}

int testCompanionPathResolution() {
    int failures = 0;

    failures += check(AnnotationStore::companionPathForPdf("sample.pdf") == "sample.xopp",
                      "simple filename companion path");
    failures += check(AnnotationStore::companionPathForPdf("/home/user/docs/test.pdf") ==
                          "/home/user/docs/test.xopp",
                      "absolute path companion resolution");
    failures +=
        check(AnnotationStore::companionPathForPdf("") == "", "empty path yields empty companion");

    return failures;
}

int testConversionFidelity() {
    int failures = 0;

    std::vector<Stroke> originalStrokes;
    Stroke s1;
    s1.id = "s1";
    s1.pageIndex = 0;
    s1.tool = "pen";
    s1.color = 0x00FF88;
    s1.width = 2.5;
    s1.pressures = {0.5, 0.9, 1.1};
    s1.points = {{0.0, 0.0}, {10.0, 15.0}, {20.0, 30.0}, {30.0, 45.0}};
    originalStrokes.push_back(s1);

    Stroke s2;
    s2.id = "s2";
    s2.pageIndex = 2; // on page index 2 (page 3)
    s2.tool = "highlighter";
    s2.color = 0xFFAABB;
    s2.width = 12.0;
    s2.points = {{50.0, 50.0}, {100.0, 100.0}};
    originalStrokes.push_back(s2);

    const std::vector<std::pair<double, double>> pageDims = {
        {612.0, 792.0}, {612.0, 792.0}, {842.0, 1191.0}};

    const XoppDocument xoppDoc =
        AnnotationStore::toXoppDocument("test.pdf", originalStrokes, pageDims);
    failures += check(xoppDoc.pages.size() == 3, "toXoppDocument generates 3 pages");
    failures += check(xoppDoc.pages[0].background.pageNumber == 1, "page 1 1-based index");
    failures += check(xoppDoc.pages[2].background.pageNumber == 3, "page 3 1-based index");
    failures += check(xoppDoc.pages[0].layers[0].strokes.size() == 1, "page 1 has 1 stroke");
    failures += check(xoppDoc.pages[1].layers[0].strokes.empty(), "page 2 has 0 strokes");
    failures += check(xoppDoc.pages[2].layers[0].strokes.size() == 1, "page 3 has 1 stroke");

    const std::vector<Stroke> restored = AnnotationStore::fromXoppDocument(xoppDoc);
    failures += check(restored.size() == 2, "restored strokes count matches");
    failures += check(restored[0].pageIndex == 0, "stroke 0 pageIndex matches");
    failures += check(restored[0].tool == "pen", "stroke 0 tool matches");
    failures += check(restored[0].color == 0x00FF88, "stroke 0 color matches");
    failures += check(restored[0].width == 2.5, "stroke 0 width matches");
    failures += check(restored[0].pressures == s1.pressures, "stroke 0 pressures match");
    failures += check(restored[0].points == s1.points, "stroke 0 points match");

    failures += check(restored[1].pageIndex == 2, "stroke 1 pageIndex matches");
    failures += check(restored[1].tool == "highlighter", "stroke 1 tool matches");
    failures += check(restored[1].color == 0xFFAABB, "stroke 1 color matches");
    failures += check(restored[1].width == 12.0, "stroke 1 width matches");
    failures += check(restored[1].points == s2.points, "stroke 1 points match");

    return failures;
}

int testSaveAndLoadRoundTrip() {
    int failures = 0;

    const std::filesystem::path pdfPath = tempFile(".pdf");
    const std::filesystem::path xoppPath = AnnotationStore::companionPathForPdf(pdfPath.string());

    AnnotationStore writerStore;
    writerStore.setPageDimensions(0, 600.0, 800.0);
    writerStore.setPageDimensions(1, 600.0, 800.0);

    Stroke s1;
    s1.tool = "pen";
    s1.color = 0xAA0011;
    s1.width = 1.5;
    s1.pressures = {1.0, 1.5};
    s1.points = {{5.0, 10.0}, {15.0, 20.0}, {25.0, 30.0}};

    Stroke s2;
    s2.tool = "pen";
    s2.color = 0x0022CC;
    s2.width = 3.0;
    s2.points = {{50.0, 50.0}};

    writerStore.addStroke(0, s1);
    writerStore.addStroke(1, s2);

    std::string saveError;
    failures += check(writerStore.saveAnnotations(pdfPath.string(), &saveError),
                      "saveAnnotations succeeds");
    failures += check(saveError.empty(), "saveAnnotations reports no error");
    failures += check(std::filesystem::exists(xoppPath), "companion .xopp file created on disk");

    AnnotationStore readerStore;
    std::string loadError;
    failures += check(readerStore.loadAnnotations(pdfPath.string(), &loadError),
                      "loadAnnotations succeeds");
    failures += check(loadError.empty(), "loadAnnotations reports no error");
    failures += check(readerStore.strokes().size() == 2, "reader holds 2 strokes");
    const std::vector<Stroke> page0Strokes = readerStore.strokesForPage(0);
    failures += check(page0Strokes.size() == 1, "reader page 0 stroke count");
    failures += check(readerStore.strokesForPage(1).size() == 1, "reader page 1 stroke count");

    if (!page0Strokes.empty()) {
        const Stroke& loadedS1 = page0Strokes[0];
        failures += check(loadedS1.tool == "pen", "loaded s1 tool");
        failures += check(loadedS1.color == 0xAA0011, "loaded s1 color");
        failures += check(loadedS1.width == 1.5, "loaded s1 width");
        failures += check(loadedS1.pressures == s1.pressures, "loaded s1 pressures");
        failures += check(loadedS1.points == s1.points, "loaded s1 points");
    }

    std::error_code ec;
    std::filesystem::remove(xoppPath, ec);
    std::filesystem::remove(pdfPath, ec);

    return failures;
}

int testMissingCompanionBehavior() {
    int failures = 0;

    AnnotationStore store;
    Stroke dummy;
    dummy.points = {{1.0, 1.0}};
    store.addStroke(0, dummy);

    std::string error;
    failures += check(store.loadAnnotations("nonexistent_document_12345.pdf", &error),
                      "load on missing companion returns true");
    failures += check(store.strokes().empty(), "load on missing companion clears stroke state");
    failures += check(error.empty(), "no error reported for absent companion");

    return failures;
}

} // namespace

int main() {
    std::srand(12345u);
    int failures = 0;
    failures += testStrokeManagement();
    failures += testCompanionPathResolution();
    failures += testConversionFidelity();
    failures += testSaveAndLoadRoundTrip();
    failures += testMissingCompanionBehavior();

    if (failures == 0) {
        std::cout << "AnnotationStoreTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
