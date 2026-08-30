#pragma once

#include "FluidCoreAPI.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace FluidCore {

struct DocumentState {
    std::vector<PageGeometry> pages;
    std::vector<SqueezeRegion> rawRegions;
    std::vector<SqueezeRegion> searchRegions;
    bool hasSearchSqueeze = false;
    std::optional<SqueezeRegion> previewRegion;
    std::vector<SqueezeSegment> segments;
    double totalDocHeight = 0.0;
    std::size_t nextRegionId = 1;
};

class SqueezeEngine {
  public:
    SqueezeEngine();
    ~SqueezeEngine() = default;

    void registerDocumentGeometry(const std::string& docId,
                                  const std::vector<PageGeometry>& pages);

    // Region management with IDs (User / Standard folds)
    std::string setSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                                 double alpha);
    void setSqueezeRegionWithId(const std::string& docId, const std::string& regionId,
                                double yStart, double yEnd, double alpha);
    bool removeSqueezeRegion(const std::string& docId, const std::string& regionId);
    void resetSqueeze(const std::string& docId);

    // Layered Search Squeeze folds (takes precedence over user folds while active)
    void setSearchSqueezeRegions(const std::string& docId, std::vector<SqueezeRegion> regions);
    void clearSearchSqueeze(const std::string& docId);
    bool isSearchSqueezeActive(const std::string& docId) const;

    // Transient preview during live drag/scroll
    void setPreviewSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                                 double alpha);
    void clearPreviewSqueezeRegion(const std::string& docId);

    // State inspection and undo/redo snapshots
    const std::vector<SqueezeRegion>& getRawRegions(const std::string& docId) const;
    void setRawRegions(const std::string& docId, const std::vector<SqueezeRegion>& regions);

    CoordinateTransformResult mapDocumentYToScreen(double docY, const std::string& docId) const;
    CoordinateTransformResult mapScreenYToDocument(double screenY, const std::string& docId) const;

    bool hasDocument(const std::string& docId) const;
    const std::vector<SqueezeSegment>& getSegments(const std::string& docId) const;
    double totalSqueezedHeight(const std::string& docId) const;

  private:
    void rebuildSegments(DocumentState& docState);
    size_t resolvePageIndex(const DocumentState& docState, double docY) const;

    std::unordered_map<std::string, DocumentState> m_documents;
};

} // namespace FluidCore
