#include "XoppDocument.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace FluidCore {

namespace {

// Zip-bomb guard: real .xopp documents decompress to a few MiB at most.
constexpr std::size_t kMaxDecompressedBytes = 256u * 1024u * 1024u;
// ---------------------------------------------------------------------------
// Number helpers
// ---------------------------------------------------------------------------

bool formatDouble(double value, char* out, std::size_t capacity) {
    // Shortest representation that survives a double round-trip exactly.
    for (int precision = 15; precision <= 17; ++precision) {
        const int written = std::snprintf(out, capacity, "%.*g", precision, value);
        if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
            return false;
        }
        double back = 0.0;
        const auto result = std::from_chars(out, out + written, back);
        if (result.ec == std::errc() && back == value) {
            return true;
        }
    }
    return false;
}

bool parseDoubleToken(std::string_view token, double& out) {
    if (token.empty()) {
        return false;
    }
    const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
    return result.ec == std::errc() && result.ptr == token.data() + token.size();
}

// Splits `text` on whitespace and parses every token as a double.
bool parseDoubleList(std::string_view text, std::vector<double>& out) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        const std::size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (start == i) {
            break;
        }
        double value = 0.0;
        if (!parseDoubleToken(text.substr(start, i - start), value)) {
            return false;
        }
        out.push_back(value);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Color helpers (.xopp serializes colors as #rrggbbaa, alpha in the low byte)
// ---------------------------------------------------------------------------

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseHexColor(std::string_view value, std::uint32_t& rgb) {
    if (value.empty() || value.front() != '#') {
        return false;
    }
    std::uint64_t packed = 0;
    int digits = 0;
    for (const char c : value.substr(1)) {
        const int d = hexDigit(c);
        if (d < 0 || digits >= 8) {
            return false;
        }
        packed = (packed << 4) | static_cast<std::uint64_t>(d);
        ++digits;
    }
    if (digits == 6) {
        rgb = static_cast<std::uint32_t>(packed); // alpha suffix absent: opaque
        return true;
    }
    if (digits == 8) {
        rgb = static_cast<std::uint32_t>(packed >> 8); // drop the alpha byte
        return true;
    }
    return false;
}

std::string serializeColor(std::uint32_t rgb) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%08x", (rgb << 8U) | 0xFFU);
    return buf;
}

// ---------------------------------------------------------------------------
// XML text helpers
// ---------------------------------------------------------------------------

void escapeXml(std::string_view text, std::string& out) {
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += c;
            break;
        }
    }
}

void appendUtf8(long codepoint, std::string& out) {
    if (codepoint < 0 || codepoint > 0x10FFFF) {
        return;
    }
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

void decodeEntities(std::string_view text, std::string& out) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&') {
            out += text[i++];
            continue;
        }
        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += text[i++]; // stray '&': keep verbatim rather than failing
            continue;
        }
        const std::string_view entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (!entity.empty() && entity.front() == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string digits(entity.substr(hex ? 2 : 1));
            appendUtf8(std::strtol(digits.c_str(), nullptr, hex ? 16 : 10), out);
        } else {
            out += text.substr(i, semi - i + 1); // unknown entity: keep verbatim
        }
        i = semi + 1;
    }
}

std::string decodeToString(std::string_view encoded) {
    std::string decoded;
    decodeEntities(encoded, decoded);
    return decoded;
}

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// ---------------------------------------------------------------------------
// Gzip container (zlib)
// ---------------------------------------------------------------------------

bool gunzip(std::string_view compressed, std::string& out, std::string& error) {
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        error = "failed to initialize zlib inflate";
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    std::vector<char> chunk(64 * 1024);
    bool success = false;
    int status = Z_OK;
    do {
        const uInt inBefore = stream.avail_in;
        const uInt outBefore = stream.avail_out;
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_END) {
            out.append(chunk.data(), chunk.size() - stream.avail_out);
            success = true;
            break;
        }
        if (status != Z_OK && status != Z_BUF_ERROR) {
            error = "corrupt or truncated gzip data";
            break;
        }
        if (stream.avail_in == inBefore && stream.avail_out == outBefore) {
            error = "truncated gzip stream"; // no progress possible
            break;
        }
        if (out.size() + (chunk.size() - stream.avail_out) > kMaxDecompressedBytes) {
            error = "decompressed .xopp exceeds the size limit";
            break;
        }
        out.append(chunk.data(), chunk.size() - stream.avail_out);
    } while (stream.avail_in > 0);

    inflateEnd(&stream);
    if (!success && error.empty()) {
        error = "truncated gzip stream";
    }
    return success;
}

bool gzipCompress(std::string_view raw, std::string& out, std::string& error) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "failed to initialize zlib deflate";
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.data()));
    stream.avail_in = static_cast<uInt>(raw.size());

    char chunk[64 * 1024];
    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(chunk);
        stream.avail_out = sizeof(chunk);
        status = deflate(&stream, Z_FINISH);
        out.append(chunk, sizeof(chunk) - stream.avail_out);
    } while (status == Z_OK);
    deflateEnd(&stream);

    if (status != Z_STREAM_END) {
        out.clear();
        error = "gzip compression failed";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Minimal XML pull-parser for the .xopp schema
//
// Tolerance policy: unknown attributes are collected and ignored, unknown or
// misplace­d known elements are skipped together with their entire subtree
// (forward compatibility with newer .xopp revisions). Structural violations
// the loader cannot recover from (bad root, mismatched tags, non-numeric
// coordinates) produce an error instead of a crash.
// ---------------------------------------------------------------------------

struct XmlAttribute {
    std::string name;
    std::string value;
};

class XoppXmlParser {
  public:
    explicit XoppXmlParser(std::string_view xml) : m_xml(xml) {}

    XoppDocument::LoadResult run() {
        XoppDocument::LoadResult result;
        try {
            if (parseDocument()) {
                result.ok = true;
                result.document = std::move(m_doc);
            } else {
                result.error = m_error.empty() ? "malformed .xopp document" : m_error;
            }
        } catch (const std::exception&) { // defensive: malformed input never escapes as a crash
            result.ok = false;
            result.error = "internal parser failure";
        }
        return result;
    }

  private:
    bool fail(const std::string& message) {
        if (m_error.empty()) {
            m_error = message;
        }
        return false;
    }

    bool done() const { return m_pos >= m_xml.size(); }

    bool startsWith(std::string_view prefix) const {
        return m_pos <= m_xml.size() && m_xml.compare(m_pos, prefix.size(), prefix) == 0;
    }

    void skipSpace() {
        while (!done() && std::isspace(static_cast<unsigned char>(m_xml[m_pos]))) {
            ++m_pos;
        }
    }

    void skipProlog() {
        if (startsWith("\xEF\xBB\xBF")) {
            m_pos += 3;
        }
        while (!done()) {
            skipSpace();
            if (startsWith("<!--") && skipUntilTerminator("-->", 4)) {
                continue;
            }
            if (startsWith("<?") && skipUntilTerminator("?>", 2)) {
                continue;
            }
            if (startsWith("<!") && skipUntilTerminator(">", 2)) {
                continue;
            }
            return;
        }
    }

    // Consumes up to and including `terminator`; returns false when absent,
    // which the caller treats as malformed input.
    bool skipUntilTerminator(std::string_view terminator, std::size_t offset) {
        const std::size_t end = m_xml.find(terminator, m_pos + offset);
        if (end == std::string_view::npos) {
            m_pos = m_xml.size();
            return false;
        }
        m_pos = end + terminator.size();
        return true;
    }

    bool parseDocument() {
        skipProlog();
        while (!done() && m_error.empty()) {
            skipSpace();
            if (done()) {
                break;
            }
            if (m_xml[m_pos] != '<') {
                collectText();
                continue;
            }
            if (startsWith("</")) {
                parseClose();
                continue;
            }
            parseOpen();
        }
        if (!m_error.empty()) {
            return false;
        }
        if (!m_sawRoot) {
            return fail("missing <xournal> root element");
        }
        if (!m_stack.empty()) {
            return fail("unclosed element(s) at end of document");
        }
        return true;
    }

    // Text content only matters inside <title> and <stroke>; elsewhere it is
    // whitespace/formatting noise and is dropped.
    void collectText() {
        const std::size_t start = m_pos;
        while (!done() && m_xml[m_pos] != '<') {
            ++m_pos;
        }
        const bool relevant =
            !m_stack.empty() &&
            ((m_stack.back() == "title" && m_stack.size() == 2 && m_stack.front() == "xournal") ||
             m_stack.back() == "stroke");
        if (relevant) {
            decodeEntities(m_xml.substr(start, m_pos - start), m_text);
        }
    }

    std::string_view readName() {
        const std::size_t start = m_pos;
        while (!done()) {
            const unsigned char c = static_cast<unsigned char>(m_xml[m_pos]);
            if (std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == '.') {
                ++m_pos;
            } else {
                break;
            }
        }
        return m_xml.substr(start, m_pos - start);
    }

    std::vector<XmlAttribute> parseAttributes() {
        std::vector<XmlAttribute> attributes;
        while (!done() && m_error.empty()) {
            skipSpace();
            if (done()) {
                fail("unterminated element");
                return attributes;
            }
            if (startsWith("/>")) {
                m_pos += 2;
                attributes.push_back({}); // sentinel: self-closing
                attributes.back().name = "/";
                return attributes;
            }
            if (m_xml[m_pos] == '>') {
                ++m_pos;
                return attributes;
            }
            const std::string attrName(readName());
            if (attrName.empty()) {
                fail("malformed attribute list");
                return attributes;
            }
            skipSpace();
            std::string value;
            if (!done() && m_xml[m_pos] == '=') {
                ++m_pos;
                skipSpace();
                if (done() || (m_xml[m_pos] != '"' && m_xml[m_pos] != '\'')) {
                    fail("attribute value must be quoted");
                    return attributes;
                }
                const char quote = m_xml[m_pos++];
                const std::size_t end = m_xml.find(quote, m_pos);
                if (end == std::string_view::npos) {
                    fail("unterminated attribute value");
                    return attributes;
                }
                value = decodeToString(m_xml.substr(m_pos, end - m_pos));
                m_pos = end + 1;
            }
            attributes.push_back({std::move(attrName), std::move(value)});
        }
        return attributes;
    }

    void parseOpen() {
        ++m_pos; // consume '<'
        const std::string name(readName());
        if (name.empty()) {
            fail("expected an element name");
            return;
        }
        const std::vector<XmlAttribute> attributes = parseAttributes();
        if (!m_error.empty()) {
            return;
        }
        const bool selfClosing = !attributes.empty() && attributes.back().name == "/";

        m_stack.push_back(name);
        m_text.clear();

        if (m_sawRoot) {
            handleElement(name, attributes);
        } else if (name == "xournal") {
            m_sawRoot = true;
            if (const XmlAttribute* creator = findAttr(attributes, "creator")) {
                m_doc.creator = creator->value;
            }
        } else {
            fail("unexpected root element <" + name + "> (expected <xournal>)");
            return;
        }

        if (selfClosing) {
            closeCurrentElement();
        }
    }

    void parseClose() {
        m_pos += 2; // consume '</'
        const std::string name(readName());
        skipSpace();
        if (done() || m_xml[m_pos] != '>') {
            fail("malformed closing tag");
            return;
        }
        ++m_pos;
        if (m_stack.empty() || m_stack.back() != name) {
            fail("mismatched closing tag </" + name + ">");
            return;
        }
        m_stack.pop_back();

        if (name == "title" && m_stack.size() == 1 && m_stack.front() == "xournal") {
            m_doc.title = trimmed(m_text);
        } else if (name == "stroke" && matchesPath({"xournal", "page", "layer"})) {
            finishStroke();
        }
        m_text.clear();
    }

    // Scope checks relative to the element being handled (already on the
    // stack): document-level children sit at depth 2, page-level at depth 3,
    // layer-level at depth 4.
    bool atDocumentScope() const { return m_stack.size() == 2 && matchesPrefix({"xournal"}); }

    bool atPageScope() const { return m_stack.size() == 3 && matchesPrefix({"xournal", "page"}); }

    bool atLayerScope() const {
        return m_stack.size() == 4 && matchesPrefix({"xournal", "page", "layer"});
    }

    bool matchesPath(const std::vector<std::string>& path) const {
        return m_stack.size() == path.size() &&
               std::equal(path.begin(), path.end(), m_stack.begin());
    }

    bool matchesPrefix(const std::vector<std::string_view>& path) const {
        return m_stack.size() >= path.size() &&
               std::equal(path.begin(), path.end(), m_stack.begin());
    }

    // Dispatches a container-opened element. Anything not modeled is ignored;
    // its children are skipped because they never match the paths above.
    void handleElement(const std::string& name, const std::vector<XmlAttribute>& attributes) {
        if (atPageScope() && name == "background") {
            parseBackground(attributes);
        } else if (atPageScope() && name == "layer") {
            XoppLayer layer;
            if (const XmlAttribute* layerName = findAttr(attributes, "name")) {
                layer.name = layerName->value;
            }
            m_doc.pages.back().layers.push_back(std::move(layer));
        } else if (atLayerScope() && name == "stroke") {
            beginStroke(attributes);
        } else if (name == "page" && atDocumentScope()) {
            XoppPage page;
            if (const XmlAttribute* width = findAttr(attributes, "width")) {
                if (!parseDoubleToken(width->value, page.width)) {
                    fail("<page> has a non-numeric width");
                    return;
                }
            }
            if (const XmlAttribute* height = findAttr(attributes, "height")) {
                if (!parseDoubleToken(height->value, page.height)) {
                    fail("<page> has a non-numeric height");
                    return;
                }
            }
            m_doc.pages.push_back(std::move(page));
        }
        // Everything else: tolerated and skipped.
    }

    void parseBackground(const std::vector<XmlAttribute>& attributes) {
        XoppBackground& bg = m_doc.pages.back().background;
        if (const XmlAttribute* type = findAttr(attributes, "type")) {
            if (type->value == "solid") {
                bg.type = XoppBackground::Type::Solid;
            } else if (type->value == "pixmap") {
                bg.type = XoppBackground::Type::Pixmap;
            } else if (type->value == "pdf") {
                bg.type = XoppBackground::Type::Pdf;
            }
        }
        if (bg.type == XoppBackground::Type::Solid) {
            if (!attrColor(attributes, "color", bg.color, bg.color)) {
                fail("<background> has a malformed color attribute");
                return;
            }
        }
        if (const XmlAttribute* style = findAttr(attributes, "style")) {
            bg.style = style->value;
        }
        if (const XmlAttribute* config = findAttr(attributes, "config")) {
            bg.config = config->value;
        }
        if (const XmlAttribute* domain = findAttr(attributes, "domain")) {
            bg.domain = domain->value;
        }
        if (const XmlAttribute* filename = findAttr(attributes, "filename")) {
            bg.filename = filename->value;
        }
        if (const XmlAttribute* pageno = findAttr(attributes, "pageno")) {
            if (!parseDoubleToken(pageno->value, m_scratchDouble)) {
                fail("<background> has a malformed pageno attribute");
                return;
            }
            bg.pageNumber = m_scratchDouble >= 1.0 ? static_cast<int>(m_scratchDouble) : 1;
        }
    }

    void beginStroke(const std::vector<XmlAttribute>& attributes) {
        m_stroke.emplace();
        if (const XmlAttribute* tool = findAttr(attributes, "tool")) {
            m_stroke->tool = tool->value;
        }
        if (!attrColor(attributes, "color", 0x000000, m_stroke->color)) {
            fail("<stroke> has a malformed color attribute");
            return;
        }
        // The width attribute is either "W" or "W P1 .. Pn" (per-segment
        // pressure values); MrWriter stores pressures in its own attribute.
        std::vector<double> widths;
        if (const XmlAttribute* width = findAttr(attributes, "width")) {
            if (!parseDoubleList(width->value, widths) || widths.empty()) {
                fail("<stroke> has a malformed width attribute");
                return;
            }
        }
        if (const XmlAttribute* pressures = findAttr(attributes, "pressures")) {
            if (!parseDoubleList(pressures->value, widths)) {
                fail("<stroke> has a malformed pressures attribute");
                return;
            }
        }
        if (!widths.empty()) {
            m_stroke->width = widths.front();
            m_stroke->pressures.assign(widths.begin() + 1, widths.end());
        }
    }

    void finishStroke() {
        if (!m_stroke) {
            return;
        }
        std::vector<double> coords;
        if (!parseDoubleList(trimmed(m_text), coords)) {
            fail("<stroke> contains a coordinate that is not a number");
            return;
        }
        if (coords.size() % 2 != 0) {
            fail("<stroke> contains an odd number of coordinates");
            return;
        }
        m_stroke->points.reserve(coords.size() / 2);
        for (std::size_t i = 0; i < coords.size(); i += 2) {
            m_stroke->points.push_back({coords[i], coords[i + 1]});
        }
        m_doc.pages.back().layers.back().strokes.push_back(std::move(*m_stroke));
        m_stroke.reset();
    }

    static const XmlAttribute* findAttr(const std::vector<XmlAttribute>& attributes,
                                        std::string_view name) {
        for (const auto& attribute : attributes) {
            if (attribute.name == name) {
                return &attribute;
            }
        }
        return nullptr;
    }

    static bool attrColor(const std::vector<XmlAttribute>& attributes, std::string_view name,
                          std::uint32_t fallback, std::uint32_t& out) {
        const XmlAttribute* attribute = findAttr(attributes, name);
        if (!attribute) {
            out = fallback;
            return true;
        }
        return parseHexColor(attribute->value, out);
    }

    void closeCurrentElement() {
        // Self-closing tags carry no text; reuse the closing-tag bookkeeping.
        if (m_stack.empty()) {
            return;
        }
        const std::string name = std::move(m_stack.back());
        m_stack.pop_back();
        if (name == "stroke" && matchesPath({"xournal", "page", "layer"}) && m_stroke) {
            // Points stay empty for "<stroke .../>"; commit what was parsed.
            m_doc.pages.back().layers.back().strokes.push_back(std::move(*m_stroke));
            m_stroke.reset();
        }
        m_text.clear();
    }

    std::string_view m_xml;
    std::size_t m_pos = 0;
    std::vector<std::string> m_stack;

    XoppDocument m_doc;
    std::optional<XoppStroke> m_stroke;
    double m_scratchDouble = 0.0;
    bool m_sawRoot = false;
    std::string m_text;
    std::string m_error;
};

std::string readFileBytes(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path + "'";
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "failed while reading '" + path + "'";
        return {};
    }
    return bytes;
}

} // namespace

// ---------------------------------------------------------------------------
// XoppDocument public API
// ---------------------------------------------------------------------------

XoppDocument::LoadResult XoppDocument::parse(std::string_view xml) {
    return XoppXmlParser(xml).run();
}

XoppDocument::LoadResult XoppDocument::load(const std::string& path) {
    LoadResult result;
    std::string error;
    const std::string compressed = readFileBytes(path, error);
    if (!error.empty()) {
        result.error = error;
        return result;
    }
    std::string xml;
    if (!gunzip(compressed, xml, error)) {
        result.error = "'" + path + "': " + error;
        return result;
    }
    result = parse(xml);
    if (!result.ok && result.error.find(path) == std::string::npos) {
        result.error = "'" + path + "': " + result.error;
    }
    return result;
}

std::string XoppDocument::serialize() const {
    std::string out;
    out += "<?xml version=\"1.0\" standalone=\"no\"?>\n";
    out += "<xournal creator=\"";
    escapeXml(creator, out);
    out += "\" fileversion=\"3\">\n";

    out += "<title>";
    escapeXml(title, out);
    out += "</title>\n";

    for (const XoppPage& page : pages) {
        out += "<page width=\"";
        char num[40];
        formatDouble(page.width, num, sizeof(num));
        out += num;
        out += "\" height=\"";
        formatDouble(page.height, num, sizeof(num));
        out += num;
        out += "\">\n";

        switch (page.background.type) {
        case XoppBackground::Type::Solid:
            out += "  <background type=\"solid\" color=\"";
            out += serializeColor(page.background.color);
            out += "\" style=\"";
            escapeXml(page.background.style, out);
            out += "\"/>\n";
            break;
        case XoppBackground::Type::Pixmap:
            out += "  <background type=\"pixmap\" domain=\"";
            escapeXml(page.background.domain, out);
            out += "\" filename=\"";
            escapeXml(page.background.filename, out);
            out += "\"/>\n";
            break;
        case XoppBackground::Type::Pdf:
            out += "  <background type=\"pdf\" domain=\"";
            escapeXml(page.background.domain, out);
            out += "\" filename=\"";
            escapeXml(page.background.filename, out);
            std::snprintf(num, sizeof(num), "%d", page.background.pageNumber);
            out += "\" pageno=\"";
            out += num;
            out += "\"/>\n";
            break;
        }

        for (const XoppLayer& layer : page.layers) {
            out += "  <layer name=\"";
            escapeXml(layer.name, out);
            out += "\">\n";

            for (const XoppStroke& stroke : layer.strokes) {
                out += "    <stroke tool=\"";
                escapeXml(stroke.tool, out);
                out += "\" color=\"";
                out += serializeColor(stroke.color);
                out += "\" width=\"";
                formatDouble(stroke.width, num, sizeof(num));
                out += num;
                if (!stroke.pressures.empty()) {
                    for (const double pressure : stroke.pressures) {
                        out += ' ';
                        formatDouble(pressure, num, sizeof(num));
                        out += num;
                    }
                }
                out += "\">";
                for (const XoppPoint& point : stroke.points) {
                    formatDouble(point.x, num, sizeof(num));
                    out += num;
                    out += ' ';
                    formatDouble(point.y, num, sizeof(num));
                    out += num;
                    out += ' ';
                }
                out += "</stroke>\n";
            }
            out += "  </layer>\n";
        }
        out += "</page>\n";
    }
    out += "</xournal>\n";
    return out;
}

bool XoppDocument::save(const std::string& path, std::string* error) const {
    std::string localError;
    std::string compressed;
    if (!gzipCompress(serialize(), compressed, localError)) {
        if (error) {
            *error = localError;
        }
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = "cannot open '" + path + "' for writing";
        }
        return false;
    }
    out.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    out.close();
    if (!out) {
        if (error) {
            *error = "failed while writing '" + path + "'";
        }
        return false;
    }
    return true;
}

} // namespace FluidCore
