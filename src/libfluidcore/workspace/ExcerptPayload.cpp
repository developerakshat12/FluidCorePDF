#include "workspace/ExcerptPayload.h"

#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace FluidCore {

namespace {

constexpr const char* kPayloadMagic = "FLUID_EXCERPT_V1";

} // namespace

std::string serializeExcerptPayload(const ExcerptDropPayload& payload) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << kPayloadMagic << "\n";
    oss << "doc:" << payload.sourceDocId.size() << ":" << payload.sourceDocId << "\n";
    oss << "page:" << payload.sourcePageNo << "\n";
    oss << std::setprecision(10);
    oss << "rect:" << payload.sourceNormalizedRect.x << " " << payload.sourceNormalizedRect.y << " "
        << payload.sourceNormalizedRect.w << " " << payload.sourceNormalizedRect.h << "\n";
    oss << "pagesize:" << payload.sourcePageWidth << " " << payload.sourcePageHeight << "\n";
    oss << "image:" << (payload.isImageExcerpt ? 1 : 0) << "\n";
    oss << "color:" << static_cast<unsigned int>(payload.color.r) << " "
        << static_cast<unsigned int>(payload.color.g) << " "
        << static_cast<unsigned int>(payload.color.b) << " "
        << static_cast<unsigned int>(payload.color.a) << "\n";
    oss << "text:" << payload.textSnippet.size() << ":" << payload.textSnippet << "\n";
    return oss.str();
}

std::optional<ExcerptDropPayload> deserializeExcerptPayload(const std::string& data) {
    if (data.size() < 16) {
        return std::nullopt;
    }

    std::string_view sv(data);
    if (!sv.starts_with(kPayloadMagic)) {
        return std::nullopt;
    }

    ExcerptDropPayload payload;
    size_t pos = std::string_view(kPayloadMagic).size();
    if (pos < sv.size() && (sv[pos] == '\r' || sv[pos] == '\n')) {
        while (pos < sv.size() && (sv[pos] == '\r' || sv[pos] == '\n')) {
            pos++;
        }
    } else {
        return std::nullopt;
    }

    bool hasDoc = false;
    bool hasPage = false;
    bool hasRect = false;

    while (pos < sv.size()) {
        if (sv.substr(pos).starts_with("doc:")) {
            pos += 4;
            size_t colon = sv.find(':', pos);
            if (colon == std::string_view::npos)
                return std::nullopt;
            size_t len = 0;
            try {
                len = std::stoull(std::string(sv.substr(pos, colon - pos)));
            } catch (...) {
                return std::nullopt;
            }
            pos = colon + 1;
            if (pos + len > sv.size())
                return std::nullopt;
            payload.sourceDocId = std::string(sv.substr(pos, len));
            pos += len;
            hasDoc = true;
        } else if (sv.substr(pos).starts_with("page:")) {
            pos += 5;
            size_t lineEnd = sv.find('\n', pos);
            std::string_view val = (lineEnd == std::string_view::npos)
                                       ? sv.substr(pos)
                                       : sv.substr(pos, lineEnd - pos);
            if (!val.empty() && val.back() == '\r')
                val.remove_suffix(1);
            try {
                payload.sourcePageNo = std::stoull(std::string(val));
                hasPage = true;
            } catch (...) {
                return std::nullopt;
            }
            pos = (lineEnd == std::string_view::npos) ? sv.size() : lineEnd;
        } else if (sv.substr(pos).starts_with("rect:")) {
            pos += 5;
            size_t lineEnd = sv.find('\n', pos);
            std::string_view val = (lineEnd == std::string_view::npos)
                                       ? sv.substr(pos)
                                       : sv.substr(pos, lineEnd - pos);
            std::string valStr(val);
            std::istringstream iss(valStr);
            iss.imbue(std::locale::classic());
            if (!(iss >> payload.sourceNormalizedRect.x >> payload.sourceNormalizedRect.y >>
                  payload.sourceNormalizedRect.w >> payload.sourceNormalizedRect.h)) {
                return std::nullopt;
            }
            hasRect = true;
            pos = (lineEnd == std::string_view::npos) ? sv.size() : lineEnd;
        } else if (sv.substr(pos).starts_with("pagesize:")) {
            pos += 9;
            size_t lineEnd = sv.find('\n', pos);
            std::string_view val = (lineEnd == std::string_view::npos)
                                       ? sv.substr(pos)
                                       : sv.substr(pos, lineEnd - pos);
            std::string valStr(val);
            std::istringstream iss(valStr);
            iss.imbue(std::locale::classic());
            double pw = 0.0, ph = 0.0;
            if (iss >> pw >> ph) {
                payload.sourcePageWidth = pw;
                payload.sourcePageHeight = ph;
            }
            pos = (lineEnd == std::string_view::npos) ? sv.size() : lineEnd;
        } else if (sv.substr(pos).starts_with("image:")) {
            pos += 6;
            size_t lineEnd = sv.find('\n', pos);
            std::string_view val = (lineEnd == std::string_view::npos)
                                       ? sv.substr(pos)
                                       : sv.substr(pos, lineEnd - pos);
            if (!val.empty() && val.back() == '\r')
                val.remove_suffix(1);
            payload.isImageExcerpt = (!val.empty() && val[0] == '1');
            pos = (lineEnd == std::string_view::npos) ? sv.size() : lineEnd;
        } else if (sv.substr(pos).starts_with("color:")) {
            pos += 6;
            size_t lineEnd = sv.find('\n', pos);
            std::string_view val = (lineEnd == std::string_view::npos)
                                       ? sv.substr(pos)
                                       : sv.substr(pos, lineEnd - pos);
            std::string valStr(val);
            std::istringstream iss(valStr);
            iss.imbue(std::locale::classic());
            unsigned int r = 255, g = 255, b = 255, a = 255;
            if (!(iss >> r >> g >> b >> a)) {
                return std::nullopt;
            }
            payload.color = {static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                             static_cast<unsigned char>(b), static_cast<unsigned char>(a)};
            pos = (lineEnd == std::string_view::npos) ? sv.size() : lineEnd;
        } else if (sv.substr(pos).starts_with("text:")) {
            pos += 5;
            size_t colon = sv.find(':', pos);
            if (colon == std::string_view::npos)
                return std::nullopt;
            size_t len = 0;
            try {
                len = std::stoull(std::string(sv.substr(pos, colon - pos)));
            } catch (...) {
                return std::nullopt;
            }
            pos = colon + 1;
            if (pos + len > sv.size())
                return std::nullopt;
            payload.textSnippet = std::string(sv.substr(pos, len));
            pos += len;
        } else {
            // Unknown key, skip to next line
            size_t nextLine = sv.find('\n', pos);
            pos = (nextLine == std::string_view::npos) ? sv.size() : nextLine;
        }

        while (pos < sv.size() && (sv[pos] == '\r' || sv[pos] == '\n')) {
            pos++;
        }
    }

    if (hasDoc && hasPage && hasRect) {
        if (payload.sourcePageWidth <= 0.0 || payload.sourcePageHeight <= 0.0) {
            payload.sourcePageWidth = 612.0;
            payload.sourcePageHeight = 792.0;
        }
        return payload;
    }
    return std::nullopt;
}

std::optional<ExcerptDropPayload> deserializeExcerptPayload(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return std::nullopt;
    }
    std::string str(reinterpret_cast<const char*>(data), length);
    return deserializeExcerptPayload(str);
}

} // namespace FluidCore
