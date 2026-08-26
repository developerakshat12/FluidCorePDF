#include "storage/AnnotationStore.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace FluidCore {

std::string AnnotationStore::companionPathForPdf(const std::string& pdfPath) {
    if (pdfPath.empty()) {
        return "";
    }
    std::filesystem::path path(pdfPath);
    path.replace_extension(".xopp");
    return path.string();
}

bool AnnotationStore::loadAnnotations(const std::string& pdfPath, std::string* error) {
    if (pdfPath.empty()) {
        clear();
        return true;
    }

    const std::string xoppPath = companionPathForPdf(pdfPath);
    std::error_code ec;
    if (!std::filesystem::exists(xoppPath, ec)) {
        clear();
        return true;
    }

    const XoppDocument::LoadResult result = XoppDocument::load(xoppPath);
    if (!result.ok) {
        if (error) {
            *error = result.error;
        }
        return false;
    }

    m_strokes = fromXoppDocument(result.document);
    m_pageDimensions.clear();
    for (const XoppPage& page : result.document.pages) {
        m_pageDimensions.push_back({page.width, page.height});
    }
    m_nextStrokeId = m_strokes.size() + 1;
    return true;
}

bool AnnotationStore::saveAnnotations(const std::string& pdfPath, std::string* error) const {
    return saveAnnotations(pdfPath, m_strokes, error);
}

bool AnnotationStore::saveAnnotations(const std::string& pdfPath,
                                      const std::vector<Stroke>& strokes,
                                      std::string* error) const {
    if (pdfPath.empty()) {
        if (error) {
            *error = "cannot save annotations: empty PDF path";
        }
        return false;
    }

    const std::string xoppPath = companionPathForPdf(pdfPath);
    const XoppDocument doc = toXoppDocument(pdfPath, strokes, m_pageDimensions);
    return doc.save(xoppPath, error);
}

std::string AnnotationStore::addStroke(std::size_t pageIdx, Stroke stroke) {
    stroke.pageIndex = pageIdx;
    if (stroke.id.empty()) {
        stroke.id = "stroke-" + std::to_string(m_nextStrokeId++);
    }
    m_strokes.push_back(std::move(stroke));
    return m_strokes.back().id;
}

bool AnnotationStore::removeStroke(const std::string& strokeId) {
    const auto it =
        std::remove_if(m_strokes.begin(), m_strokes.end(),
                       [&strokeId](const Stroke& stroke) { return stroke.id == strokeId; });
    if (it != m_strokes.end()) {
        m_strokes.erase(it, m_strokes.end());
        return true;
    }
    return false;
}

void AnnotationStore::clear() {
    m_strokes.clear();
    m_nextStrokeId = 1;
}

std::vector<Stroke> AnnotationStore::strokesForPage(std::size_t pageIdx) const {
    std::vector<Stroke> result;
    for (const Stroke& stroke : m_strokes) {
        if (stroke.pageIndex == pageIdx) {
            result.push_back(stroke);
        }
    }
    return result;
}

const Stroke* AnnotationStore::findStroke(const std::string& strokeId) const {
    for (const Stroke& stroke : m_strokes) {
        if (stroke.id == strokeId) {
            return &stroke;
        }
    }
    return nullptr;
}

void AnnotationStore::setPageDimensions(std::size_t pageIdx, double width, double height) {
    if (pageIdx >= m_pageDimensions.size()) {
        m_pageDimensions.resize(pageIdx + 1, {595.0, 842.0});
    }
    m_pageDimensions[pageIdx] = {width, height};
}

void AnnotationStore::registerPageCount(std::size_t count) {
    if (count > m_pageDimensions.size()) {
        m_pageDimensions.resize(count, {595.0, 842.0});
    }
}

XoppDocument
AnnotationStore::toXoppDocument(const std::string& pdfPath, const std::vector<Stroke>& strokes,
                                const std::vector<std::pair<double, double>>& pageDimensions) {
    XoppDocument doc;
    doc.creator = "FluidCore";

    std::size_t numPages = pageDimensions.size();
    for (const Stroke& stroke : strokes) {
        if (stroke.pageIndex + 1 > numPages) {
            numPages = stroke.pageIndex + 1;
        }
    }
    if (numPages == 0 && !strokes.empty()) {
        numPages = 1;
    }

    doc.pages.reserve(numPages);
    for (std::size_t p = 0; p < numPages; ++p) {
        XoppPage page;
        if (p < pageDimensions.size()) {
            page.width = pageDimensions[p].first;
            page.height = pageDimensions[p].second;
        } else {
            page.width = 595.0;
            page.height = 842.0;
        }

        page.background.type = XoppBackground::Type::Pdf;
        page.background.domain = "absolute";
        page.background.filename = pdfPath;
        page.background.pageNumber = static_cast<int>(p + 1);

        XoppLayer layer;
        layer.name = "Default";
        for (const Stroke& stroke : strokes) {
            if (stroke.pageIndex == p) {
                XoppStroke xStroke;
                xStroke.tool = stroke.tool;
                xStroke.color = stroke.color;
                xStroke.width = stroke.width;
                xStroke.pressures = stroke.pressures;
                xStroke.points = stroke.points;
                layer.strokes.push_back(std::move(xStroke));
            }
        }
        page.layers.push_back(std::move(layer));
        doc.pages.push_back(std::move(page));
    }

    return doc;
}

std::vector<Stroke> AnnotationStore::fromXoppDocument(const XoppDocument& doc) {
    std::vector<Stroke> strokes;
    std::size_t counter = 1;

    for (std::size_t p = 0; p < doc.pages.size(); ++p) {
        const XoppPage& page = doc.pages[p];
        for (const XoppLayer& layer : page.layers) {
            for (const XoppStroke& xStroke : layer.strokes) {
                Stroke stroke;
                stroke.id = "stroke-" + std::to_string(counter++);
                stroke.pageIndex = p;
                stroke.tool = xStroke.tool;
                stroke.color = xStroke.color;
                stroke.width = xStroke.width;
                stroke.pressures = xStroke.pressures;
                stroke.points = xStroke.points;
                strokes.push_back(std::move(stroke));
            }
        }
    }

    return strokes;
}

} // namespace FluidCore
