#include "SqueezeEngine.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace FluidCore {

namespace {

constexpr double kEps = 1e-9;

double clampAlpha(double alpha) {
    if (alpha < kSqueezeMinAlpha) {
        return kSqueezeMinAlpha;
    }
    if (alpha > kSqueezeMaxAlpha) {
        return kSqueezeMaxAlpha;
    }
    return alpha;
}

} // namespace

SqueezeEngine::SqueezeEngine() = default;

void SqueezeEngine::registerDocumentGeometry(const std::string& docId,
                                             const std::vector<PageGeometry>& pages) {
    auto& doc = m_documents[docId];
    doc.pages = pages;
    doc.totalDocHeight = 0.0;
    for (const auto& page : pages) {
        double bottom = page.unscaledYOffset + page.heightPt;
        if (bottom > doc.totalDocHeight) {
            doc.totalDocHeight = bottom;
        }
    }
    rebuildSegments(doc);
}

std::string SqueezeEngine::setSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                                            double alpha) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    std::string regionId = "sq-" + std::to_string(it->second.nextRegionId++);
    setSqueezeRegionWithId(docId, regionId, yStart, yEnd, alpha);
    return regionId;
}

void SqueezeEngine::setSqueezeRegionWithId(const std::string& docId, const std::string& regionId,
                                           double yStart, double yEnd, double alpha) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    if (yStart > yEnd) {
        std::swap(yStart, yEnd);
    }
    if (it->second.totalDocHeight > 0.0) {
        yStart = std::clamp(yStart, 0.0, it->second.totalDocHeight);
        yEnd = std::clamp(yEnd, 0.0, it->second.totalDocHeight);
    } else {
        yStart = std::max(0.0, yStart);
        yEnd = std::max(0.0, yEnd);
    }
    if (std::abs(yEnd - yStart) < kEps) {
        return;
    }

    auto& regions = it->second.rawRegions;
    auto regIt = std::find_if(regions.begin(), regions.end(),
                              [&regionId](const SqueezeRegion& r) { return r.id == regionId; });

    if (regIt != regions.end()) {
        regIt->yStart = yStart;
        regIt->yEnd = yEnd;
        regIt->alpha = clampAlpha(alpha);
    } else {
        regions.push_back(SqueezeRegion{regionId, yStart, yEnd, clampAlpha(alpha)});
    }

    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

bool SqueezeEngine::removeSqueezeRegion(const std::string& docId, const std::string& regionId) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    auto& regions = it->second.rawRegions;
    auto regIt = std::remove_if(regions.begin(), regions.end(),
                                [&regionId](const SqueezeRegion& r) { return r.id == regionId; });
    if (regIt != regions.end()) {
        regions.erase(regIt, regions.end());
        it->second.previewRegion.reset();
        rebuildSegments(it->second);
        return true;
    }
    return false;
}

void SqueezeEngine::resetSqueeze(const std::string& docId) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    it->second.rawRegions.clear();
    it->second.searchRegions.clear();
    it->second.hasSearchSqueeze = false;
    it->second.highlightRegions.clear();
    it->second.hasHighlightSqueeze = false;
    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

std::optional<SqueezeRegion> SqueezeEngine::findFoldRegionAt(const std::string& docId, double docY,
                                                             double tolerance) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        return std::nullopt;
    }

    for (const auto& r : it->second.rawRegions) {
        if (docY >= (r.yStart - tolerance) && docY <= (r.yEnd + tolerance)) {
            return r;
        }
    }
    return std::nullopt;
}

bool SqueezeEngine::updateFoldAlpha(const std::string& docId, const std::string& regionId,
                                    double alpha) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        return false;
    }

    auto& regions = it->second.rawRegions;
    auto regIt = std::find_if(regions.begin(), regions.end(),
                              [&regionId](const SqueezeRegion& r) { return r.id == regionId; });
    if (regIt != regions.end()) {
        regIt->alpha = clampAlpha(alpha);
        rebuildSegments(it->second);
        return true;
    }
    return false;
}

void SqueezeEngine::setSearchSqueezeRegions(const std::string& docId,
                                            std::vector<SqueezeRegion> regions) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    it->second.searchRegions = std::move(regions);
    it->second.hasSearchSqueeze = true;
    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

void SqueezeEngine::clearSearchSqueeze(const std::string& docId) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    it->second.searchRegions.clear();
    it->second.hasSearchSqueeze = false;
    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

bool SqueezeEngine::isSearchSqueezeActive(const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        return false;
    }
    return it->second.hasSearchSqueeze;
}

void SqueezeEngine::setHighlightSqueezeRegions(const std::string& docId,
                                               std::vector<SqueezeRegion> regions) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    it->second.highlightRegions = std::move(regions);
    it->second.hasHighlightSqueeze = true;
    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

void SqueezeEngine::clearHighlightSqueeze(const std::string& docId) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    it->second.highlightRegions.clear();
    it->second.hasHighlightSqueeze = false;
    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

bool SqueezeEngine::isHighlightSqueezeActive(const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        return false;
    }
    return it->second.hasHighlightSqueeze;
}

void SqueezeEngine::setPreviewSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                                            double alpha) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    if (yStart > yEnd) {
        std::swap(yStart, yEnd);
    }
    if (it->second.totalDocHeight > 0.0) {
        yStart = std::clamp(yStart, 0.0, it->second.totalDocHeight);
        yEnd = std::clamp(yEnd, 0.0, it->second.totalDocHeight);
    } else {
        yStart = std::max(0.0, yStart);
        yEnd = std::max(0.0, yEnd);
    }

    if (std::abs(yEnd - yStart) < kEps) {
        it->second.previewRegion.reset();
    } else {
        it->second.previewRegion = SqueezeRegion{"preview", yStart, yEnd, clampAlpha(alpha)};
    }
    rebuildSegments(it->second);
}

void SqueezeEngine::clearPreviewSqueezeRegion(const std::string& docId) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    if (it->second.previewRegion.has_value()) {
        it->second.previewRegion.reset();
        rebuildSegments(it->second);
    }
}

const std::vector<SqueezeRegion>& SqueezeEngine::getRawRegions(const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }
    return it->second.rawRegions;
}

void SqueezeEngine::setRawRegions(const std::string& docId,
                                  const std::vector<SqueezeRegion>& regions) {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    it->second.rawRegions = regions;
    it->second.previewRegion.reset();
    rebuildSegments(it->second);
}

CoordinateTransformResult SqueezeEngine::mapDocumentYToScreen(double docY,
                                                              const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    const auto& doc = it->second;
    size_t pageIdx = resolvePageIndex(doc, docY);

    if (doc.segments.empty()) {
        return CoordinateTransformResult{docY, pageIdx, 1.0};
    }

    if (docY < doc.segments.front().docYStart) {
        double sY = doc.segments.front().screenYStart + (docY - doc.segments.front().docYStart);
        return CoordinateTransformResult{sY, pageIdx, 1.0};
    }

    if (docY >= doc.segments.back().docYEnd) {
        double sY = doc.segments.back().screenYEnd + (docY - doc.segments.back().docYEnd);
        return CoordinateTransformResult{sY, pageIdx, 1.0};
    }

    auto segIt =
        std::upper_bound(doc.segments.begin(), doc.segments.end(), docY,
                         [](double val, const SqueezeSegment& seg) { return val < seg.docYEnd; });

    if (segIt != doc.segments.end()) {
        double sY = segIt->screenYStart + (docY - segIt->docYStart) * segIt->alpha;
        return CoordinateTransformResult{sY, pageIdx, segIt->alpha};
    }

    return CoordinateTransformResult{docY, pageIdx, 1.0};
}

CoordinateTransformResult SqueezeEngine::mapScreenYToDocument(double screenY,
                                                              const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }

    const auto& doc = it->second;

    if (doc.segments.empty()) {
        size_t pageIdx = resolvePageIndex(doc, screenY);
        return CoordinateTransformResult{screenY, pageIdx, 1.0};
    }

    if (screenY < doc.segments.front().screenYStart) {
        double dY = doc.segments.front().docYStart + (screenY - doc.segments.front().screenYStart);
        size_t pageIdx = resolvePageIndex(doc, dY);
        return CoordinateTransformResult{dY, pageIdx, 1.0};
    }

    if (screenY >= doc.segments.back().screenYEnd) {
        double dY = doc.segments.back().docYEnd + (screenY - doc.segments.back().screenYEnd);
        size_t pageIdx = resolvePageIndex(doc, dY);
        return CoordinateTransformResult{dY, pageIdx, 1.0};
    }

    auto segIt = std::upper_bound(
        doc.segments.begin(), doc.segments.end(), screenY,
        [](double val, const SqueezeSegment& seg) { return val < seg.screenYEnd; });

    if (segIt != doc.segments.end()) {
        double dY = segIt->docYStart + (screenY - segIt->screenYStart) / segIt->alpha;
        size_t pageIdx = resolvePageIndex(doc, dY);
        return CoordinateTransformResult{dY, pageIdx, segIt->alpha};
    }

    size_t pageIdx = resolvePageIndex(doc, screenY);
    return CoordinateTransformResult{screenY, pageIdx, 1.0};
}

bool SqueezeEngine::hasDocument(const std::string& docId) const {
    return m_documents.find(docId) != m_documents.end();
}

const std::vector<SqueezeSegment>& SqueezeEngine::getSegments(const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }
    return it->second.segments;
}

double SqueezeEngine::totalSqueezedHeight(const std::string& docId) const {
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        throw std::invalid_argument("Document ID not registered: " + docId);
    }
    if (it->second.segments.empty()) {
        return it->second.totalDocHeight;
    }
    return it->second.segments.back().screenYEnd;
}

void SqueezeEngine::rebuildSegments(DocumentState& docState) {
    docState.segments.clear();

    std::vector<SqueezeRegion> effectiveRegions;
    if (docState.hasSearchSqueeze) {
        effectiveRegions = docState.searchRegions;
    } else if (docState.hasHighlightSqueeze) {
        effectiveRegions = docState.highlightRegions;
    } else {
        effectiveRegions = docState.rawRegions;
        if (docState.previewRegion.has_value()) {
            effectiveRegions.push_back(*docState.previewRegion);
        }
    }

    std::set<double> points;
    points.insert(0.0);
    if (docState.totalDocHeight > 0.0) {
        points.insert(docState.totalDocHeight);
    }
    for (const auto& r : effectiveRegions) {
        double y0 = r.yStart;
        double y1 = r.yEnd;
        if (docState.totalDocHeight > 0.0) {
            y0 = std::clamp(y0, 0.0, docState.totalDocHeight);
            y1 = std::clamp(y1, 0.0, docState.totalDocHeight);
        } else {
            y0 = std::max(0.0, y0);
            y1 = std::max(0.0, y1);
        }
        points.insert(y0);
        points.insert(y1);
    }

    if (points.size() < 2) {
        if (docState.totalDocHeight > 0.0) {
            docState.segments.push_back(
                SqueezeSegment{0.0, docState.totalDocHeight, 0.0, docState.totalDocHeight, 1.0});
        }
        return;
    }

    std::vector<double> sortedPoints(points.begin(), points.end());
    std::vector<SqueezeSegment> rawSegments;

    for (size_t i = 0; i + 1 < sortedPoints.size(); ++i) {
        double y0 = sortedPoints[i];
        double y1 = sortedPoints[i + 1];
        if (y1 - y0 < kEps) {
            continue;
        }

        double mid = (y0 + y1) / 2.0;
        double effectiveAlpha = 1.0;
        for (const auto& r : effectiveRegions) {
            if (r.yStart <= mid && mid <= r.yEnd) {
                if (r.alpha < effectiveAlpha) {
                    effectiveAlpha = r.alpha;
                }
            }
        }

        if (!rawSegments.empty() && std::abs(rawSegments.back().alpha - effectiveAlpha) < kEps) {
            rawSegments.back().docYEnd = y1;
        } else {
            rawSegments.push_back(SqueezeSegment{y0, y1, 0.0, 0.0, effectiveAlpha});
        }
    }

    double curScreenY = 0.0;
    for (auto& seg : rawSegments) {
        seg.screenYStart = curScreenY;
        double docLen = seg.docYEnd - seg.docYStart;
        double screenLen = docLen * seg.alpha;
        seg.screenYEnd = curScreenY + screenLen;
        curScreenY = seg.screenYEnd;
    }

    docState.segments = std::move(rawSegments);
}

size_t SqueezeEngine::resolvePageIndex(const DocumentState& docState, double docY) const {
    if (docState.pages.empty()) {
        return 0;
    }
    if (docY <= docState.pages.front().unscaledYOffset) {
        return docState.pages.front().pageIndex;
    }
    if (docY >= docState.pages.back().unscaledYOffset + docState.pages.back().heightPt) {
        return docState.pages.back().pageIndex;
    }

    for (size_t i = 0; i < docState.pages.size(); ++i) {
        const auto& page = docState.pages[i];
        double pageTop = page.unscaledYOffset;
        double pageBottom = pageTop + page.heightPt;
        if (docY >= pageTop && docY <= pageBottom) {
            return page.pageIndex;
        }
        if (i + 1 < docState.pages.size()) {
            double nextTop = docState.pages[i + 1].unscaledYOffset;
            if (docY > pageBottom && docY < nextTop) {
                return page.pageIndex;
            }
        }
    }
    return docState.pages.back().pageIndex;
}

} // namespace FluidCore
