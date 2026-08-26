#pragma once

#include "storage/XoppDocument.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace FluidCore {

// FluidCore workspace stroke model. Pure C++20, no GUI types (ADR-0001).
// Serializable to XoppDocument / .xopp without loss.
struct Stroke {
    std::string id;
    std::size_t pageIndex = 0;
    std::string tool = "pen";
    std::uint32_t color = 0x000000;
    double width = 1.0;
    std::vector<double> pressures;
    std::vector<XoppPoint> points;
    std::uint64_t timestamp = 0;

    bool operator==(const Stroke&) const = default;
};

// C++20 wrapper around XoppDocument mapping workspace strokes to XoppPage strokes
// and managing .xopp companion persistence.
class AnnotationStore {
  public:
    AnnotationStore() = default;

    // Resolves companion path (<pdfPath>.xopp) and loads annotations.
    // If companion does not exist, clears internal strokes and returns true.
    // On parse/read failure, returns false and populates error if non-null.
    bool loadAnnotations(const std::string& pdfPath, std::string* error = nullptr);

    // Saves currently held strokes to companion path (<pdfPath>.xopp).
    bool saveAnnotations(const std::string& pdfPath, std::string* error = nullptr) const;

    // Saves provided strokes to companion path (<pdfPath>.xopp).
    bool saveAnnotations(const std::string& pdfPath, const std::vector<Stroke>& strokes,
                         std::string* error = nullptr) const;

    // Stroke management
    std::string addStroke(std::size_t pageIdx, Stroke stroke);
    bool removeStroke(const std::string& strokeId);
    void clear();

    const std::vector<Stroke>& strokes() const { return m_strokes; }
    std::vector<Stroke> strokesForPage(std::size_t pageIdx) const;
    const Stroke* findStroke(const std::string& strokeId) const;

    // Page geometry metadata for .xopp page background generation
    void setPageDimensions(std::size_t pageIdx, double width, double height);
    void registerPageCount(std::size_t count);
    const std::vector<std::pair<double, double>>& pageDimensions() const {
        return m_pageDimensions;
    }

    // Helper to resolve companion .xopp path for a PDF
    static std::string companionPathForPdf(const std::string& pdfPath);

    // Bidirectional conversion between Stroke collections and XoppDocument
    static XoppDocument
    toXoppDocument(const std::string& pdfPath, const std::vector<Stroke>& strokes,
                   const std::vector<std::pair<double, double>>& pageDimensions = {});

    static std::vector<Stroke> fromXoppDocument(const XoppDocument& doc);

  private:
    std::vector<Stroke> m_strokes;
    std::vector<std::pair<double, double>> m_pageDimensions;
    std::size_t m_nextStrokeId = 1;
};

} // namespace FluidCore
