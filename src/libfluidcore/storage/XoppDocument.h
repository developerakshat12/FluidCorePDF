#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace FluidCore {

// Legacy .xopp persistence model (.xopp = gzipped XML with an <xournal> root).
// Pure C++20, no GUI types (ADR-0001). Deliberately lossy-by-design: this bridge
// keeps pages/layers/strokes (the ink skeleton) and ignores richer elements it
// does not model yet, so forward-compatibility only requires tolerating them.
struct XoppPoint {
    double x = 0.0;
    double y = 0.0;
    bool operator==(const XoppPoint&) const = default;
};

// A hand-drawn stroke. `color` is 24-bit RGB; alpha is implicit (highlighter
// translucency lives in the tool semantics, matching the .xopp convention of
// serializing colors as #rrggbbaa with a fixed alpha per tool).
// When `pressures` is non-empty it has exactly points.size() - 1 entries
// (one per segment, none trailing the last vertex).
struct XoppStroke {
    std::string tool = "pen";
    std::uint32_t color = 0x000000;
    double width = 1.0;
    std::vector<double> pressures;
    std::vector<XoppPoint> points;
    bool operator==(const XoppStroke&) const = default;
};

struct XoppBackground {
    enum class Type { Solid, Pixmap, Pdf };

    Type type = Type::Solid;
    std::uint32_t color = 0xFFFFFF; // solid backgrounds only
    std::string style;              // e.g. "plain", "lined", "graph"
    std::string config;             // optional format-specific config string
    std::string domain;             // pixmap/pdf: "absolute", "attach" or "clone"
    std::string filename;           // pixmap/pdf: referenced asset
    int pageNumber = 1;             // pdf: 1-based pdf page index
    bool operator==(const XoppBackground&) const = default;
};

struct XoppLayer {
    std::string name;
    std::vector<XoppStroke> strokes;
    bool operator==(const XoppLayer&) const = default;
};

struct XoppPage {
    double width = 595.0;
    double height = 842.0;
    XoppBackground background;
    std::vector<XoppLayer> layers;
    bool operator==(const XoppPage&) const = default;
};

class XoppDocument {
  public:
    struct LoadResult;

    std::string creator;
    std::string title;
    std::vector<XoppPage> pages;

    // Parses uncompressed .xopp XML (the inner payload of a .xopp file).
    // Tolerant of unknown attributes and unknown child tags; strict about
    // structure it relies on. Malformed input yields {ok=false, error}, never
    // an exception or crash.
    static LoadResult parse(std::string_view xml);

    // Full persistence loop over a gzipped .xopp file.
    static LoadResult load(const std::string& path);
    bool save(const std::string& path, std::string* error = nullptr) const;

    // The uncompressed XML payload `serialize()` emits and `parse()` accepts.
    std::string serialize() const;

    bool operator==(const XoppDocument&) const = default;
};

struct XoppDocument::LoadResult {
    bool ok = false;
    XoppDocument document;
    std::string error; // human-readable reason when !ok
};

} // namespace FluidCore
