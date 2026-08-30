#pragma once

#include "FluidCoreAPI.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace FluidCore {

// Structured payload transferred across drag-and-drop boundary via MIME
// application/x-fluid-excerpt (specs/integration.md §2, ADR-0001).
// Encapsulates normalized bounding box, page index, source document, and snippet.
struct ExcerptDropPayload {
    std::string sourceDocId;
    size_t sourcePageNo = 0;
    Rectangle sourceNormalizedRect{0.0, 0.0, 1.0, 1.0};
    std::string textSnippet;
    bool isImageExcerpt = false;
    Color color{255, 255, 255, 255};
};

std::string serializeExcerptPayload(const ExcerptDropPayload& payload);
std::optional<ExcerptDropPayload> deserializeExcerptPayload(const std::string& data);
std::optional<ExcerptDropPayload> deserializeExcerptPayload(const uint8_t* data, size_t length);

} // namespace FluidCore
