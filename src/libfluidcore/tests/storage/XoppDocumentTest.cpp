// Headless unit tests for the .xopp persistence slice (mirrors
// storage/XoppDocument.cpp): round-trip fidelity plus malformed-input
// robustness.

#include "storage/XoppDocument.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
           ("fluidcore_xopp_test_" + std::to_string(counter++) + "_" + std::to_string(std::rand()) +
            suffix);
}

XoppDocument makeSampleDocument() {
    XoppDocument doc;
    doc.creator = "FluidCore XoppDocumentTest";
    doc.title = "Round-trip sample \xE2\x80\x94 with unicode & <markup>";

    XoppPage page;
    page.width = 794.0;
    page.height = 1123.5;
    page.background.type = XoppBackground::Type::Solid;
    page.background.color = 0xFFFF7F;
    page.background.style = "lined";

    XoppLayer ink;
    ink.name = "Layer 1";

    // Plain stroke: single width, three points.
    XoppStroke plain;
    plain.tool = "pen";
    plain.color = 0x1A2B3C;
    plain.width = 1.75;
    plain.points = {{10.25, -20.5}, {30.125, 40.0}, {50.0, 60.000001}};

    // Pressure stroke: one pressure per segment.
    XoppStroke pressured;
    pressured.tool = "highlighter";
    pressured.color = 0xFFEEDD;
    pressured.width = 12.5;
    pressured.pressures = {0.5, 2.25};
    pressured.points = {{0.0, 0.0}, {100.0, 0.5}, {200.0, 300.75}};

    // Single-point dot, no pressures.
    XoppStroke dot;
    dot.tool = "pen";
    dot.color = 0x000000;
    dot.width = 3.5;
    dot.points = {{42.0, 24.0}};

    ink.strokes = {plain, pressured, dot};

    XoppLayer empty;
    empty.name = "";

    page.layers = {ink, empty};
    doc.pages.push_back(page);

    XoppPage pdfPage;
    pdfPage.width = 612.0;
    pdfPage.height = 792.0;
    pdfPage.background.type = XoppBackground::Type::Pdf;
    pdfPage.background.domain = "absolute";
    pdfPage.background.filename = "books/paper.pdf";
    pdfPage.background.pageNumber = 3;
    doc.pages.push_back(pdfPage);

    return doc;
}

int testRoundTrip() {
    int failures = 0;
    const XoppDocument original = makeSampleDocument();

    const std::filesystem::path path = tempFile(".xopp");
    std::string saveError;
    failures += check(original.save(path.string(), &saveError), "save() succeeds");
    failures += check(saveError.empty(), "save() reports no error");
    failures += check(std::filesystem::file_size(path) > 0, "written file is not empty");

    const XoppDocument::LoadResult loaded = XoppDocument::load(path.string());
    failures += check(loaded.ok, "load() succeeds on a file we just wrote");
    if (!loaded.ok) {
        std::cerr << "  load error: " << loaded.error << "\n";
        std::filesystem::remove(path);
        return failures;
    }
    failures += check(loaded.document == original, "round trip restores an identical document");
    failures += check(loaded.error.empty(), "successful load carries no error text");

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return failures;
}

int testSerializedShape() {
    int failures = 0;
    const std::string xml = makeSampleDocument().serialize();
    failures += check(xml.find("<?xml version=\"1.0\" standalone=\"no\"?>") == 0,
                      "serialization starts with an XML declaration");
    failures +=
        check(xml.find("<xournal creator=") != std::string::npos, "root element serializes");
    failures +=
        check(xml.find("tool=\"highlighter\"") != std::string::npos, "stroke tool serializes");
    failures += check(xml.find("width=\"12.5 0.5 2.25\"") != std::string::npos,
                      "pressures serialize inside the width attribute");
    failures += check(xml.find("type=\"pdf\"") != std::string::npos &&
                          xml.find("pageno=\"3\"") != std::string::npos,
                      "pdf background serializes with its page number");
    failures += check(xml.find("&amp;") != std::string::npos &&
                          xml.find("&lt;markup&gt;") != std::string::npos,
                      "title text is XML-escaped");
    return failures;
}

int testUnknownElementsAndAttributesAreTolerated() {
    int failures = 0;

    XoppStroke stroke;
    stroke.color = 0xABCDEF; // #abcdef01: alpha suffix is dropped by the model
    stroke.width = 2.0;
    stroke.points = {{1.0, 2.0}, {3.0, 4.0}};
    XoppLayer layer;
    layer.name = "L";
    layer.strokes.push_back(stroke);

    const std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<xournal creator=\"future-app\" fileversion=\"99\" future-flag=\"yes\">\n"
        "  <preview>base64data</preview>\n"
        "  <title>tolerant</title>\n"
        "  <page width=\"100\" height=\"200\">\n"
        "    <background type=\"solid\" color=\"#123456ff\" style=\"plain\" config=\"x\"/>\n"
        "    <future-element attr=\"ignored\"><nested deeper=\"still-ignored\"/></future-element>\n"
        "    <layer name=\"L\" revision=\"7\">\n"
        "      <audio timestamp=\"12\" fn=\"clip.wav\"/>\n"
        "      <stroke tool=\"pen\" color=\"#abcdef01\" width=\"2\" capStyle=\"butt\" "
        "future-stroke-attr=\"v\">1 2 3 4</stroke>\n"
        "    </layer>\n"
        "  </page>\n"
        "</xournal>\n";

    const XoppDocument::LoadResult result = XoppDocument::parse(xml);
    failures += check(result.ok, "unknown attributes/elements parse cleanly");
    if (!result.ok) {
        std::cerr << "  error: " << result.error << "\n";
        return failures;
    }
    failures += check(result.document.creator == "future-app", "creator attribute is read");
    failures += check(result.document.title == "tolerant", "title is read");
    failures += check(result.document.pages.size() == 1, "page count matches");
    failures += check(result.document.pages[0].background.color == 0x123456,
                      "alpha byte of #rrggbbaa colors is dropped");
    failures += check(result.document.pages[0].layers.size() == 1, "only modeled layers survive");
    failures += check(result.document.pages[0].layers[0].strokes.size() == 1,
                      "only modeled strokes survive");
    failures +=
        check(result.document.pages[0].layers[0].strokes[0] == stroke, "stroke content matches");
    return failures;
}

int testMalformedInputs() {
    int failures = 0;

    // Missing file.
    XoppDocument::LoadResult result = XoppDocument::load("definitely/not/a/real/file.xopp");
    failures += check(!result.ok && !result.error.empty(), "missing file fails gracefully");

    // Not gzip at all.
    const std::filesystem::path garbagePath = tempFile(".xopp");
    {
        std::ofstream out(garbagePath, std::ios::binary);
        out << "\x1f\x8bNOT-A-REAL-GZIP-STREAM-just-garbage-bytes";
    }
    result = XoppDocument::load(garbagePath.string());
    failures += check(!result.ok && !result.error.empty(), "garbage payload fails gracefully");
    std::error_code ec;
    std::filesystem::remove(garbagePath, ec);

    // Valid gzip header but truncated deflate stream: take a real serialized
    // document's bytes and cut them in half.
    const std::filesystem::path truncatedPath = tempFile(".xopp");
    {
        const std::string compressed = [&] {
            const XoppDocument doc = makeSampleDocument();
            std::string error;
            std::string bytes;
            // serialize + gzip via save(); read back what save wrote.
            doc.save(truncatedPath.string(), &error);
            std::ifstream in(truncatedPath, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return bytes;
        }();
        std::ofstream out(truncatedPath, std::ios::binary | std::ios::trunc);
        out.write(compressed.data(), static_cast<std::streamsize>(compressed.size() / 2));
    }
    result = XoppDocument::load(truncatedPath.string());
    failures +=
        check(!result.ok && !result.error.empty(), "truncated gzip stream fails gracefully");
    std::filesystem::remove(truncatedPath, ec);

    // Structurally broken XML payloads (gzip-wrapped by hand via round-trip
    // through parse() is impossible here, so exercise the parser directly).
    struct Case {
        const char* xml;
        const char* what;
    };
    const Case cases[] = {
        {"<html><body>not a document</body></html>", "wrong root element"},
        {"<xournal><title>unclosed", "unclosed root element"},
        {"<xournal><page width=\"100\"><layer><stroke tool=\"pen\" color=\"#000000\" "
         "width=\"1\">1 2 3</stroke></layer></page></xournal>",
         "odd coordinate count"},
        {"<xournal><page width=\"abc\" height=\"200\"></page></xournal>", "non-numeric page width"},
        {"<xournal><page><layer><stroke tool=\"pen\" width=\"1\" color=\"#nothex\">0 0</stroke>"
         "</layer></page></xournal>",
         "malformed color"},
        {"<xournal></other></xournal>", "mismatched closing tag"},
        {"", "empty payload"},
    };
    for (const Case& testCase : cases) {
        result = XoppDocument::parse(testCase.xml);
        failures += check(!result.ok, testCase.what);
        failures += check(!result.error.empty(), "every failure carries an error message");
    }

    // A valid document must still load after all the failures above.
    const XoppDocument::LoadResult healthy = XoppDocument::parse(makeSampleDocument().serialize());
    failures += check(healthy.ok, "parser still works after malformed inputs");
    return failures;
}

int testEmptyAndDegenerateDocuments() {
    int failures = 0;

    XoppDocument doc;
    doc.creator = "t";
    const std::filesystem::path path = tempFile(".xopp");
    std::string error;
    failures += check(doc.save(path.string(), &error), "zero-page document saves");
    const XoppDocument::LoadResult result = XoppDocument::load(path.string());
    failures += check(result.ok && result.document.pages.empty(), "zero-page document round-trips");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Self-closing stroke with no points survives.
    const XoppDocument::LoadResult selfClosing =
        XoppDocument::parse("<xournal><page width=\"10\" height=\"10\">"
                            "<background type=\"solid\" style=\"plain\"/>"
                            "<layer><stroke tool=\"pen\" width=\"1\"/></layer>"
                            "</page></xournal>");
    failures += check(selfClosing.ok, "self-closing stroke parses");
    if (selfClosing.ok) {
        const auto& strokes = selfClosing.document.pages.at(0).layers.at(0).strokes;
        failures += check(strokes.size() == 1 && strokes[0].points.empty(),
                          "self-closing stroke has no points");
    }
    return failures;
}

} // namespace

int main() {
    std::srand(42u);
    int failures = 0;
    failures += testRoundTrip();
    failures += testSerializedShape();
    failures += testUnknownElementsAndAttributesAreTolerated();
    failures += testMalformedInputs();
    failures += testEmptyAndDegenerateDocuments();

    if (failures == 0) {
        std::cout << "XoppDocumentTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
