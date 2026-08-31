// FluidCoreAPI.h — public engine boundary consumed by src/app.
//
// Method signatures transcribed verbatim from TRD §4.1. Supporting value types
// below are signature-level stubs: field choices are provisional and carry no
// persistence commitment. Authority/provenance: planning/decisions/ADR-0002.
//
// Boundary rules (ADR-0001): no GTK/GDK/GLib/Cairo/Poppler headers or types may
// appear here or anywhere under libfluidcore/. All rendering lives in the GUI layer.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace FluidCore {

inline constexpr const char* kFluidCoreVersion = "0.0.0-bootstrap";

// TRD §4.1
struct PageGeometry {
    size_t pageIndex;
    double widthPt;
    double heightPt;
    double unscaledYOffset;
};

// Provisional value-type stubs backing the TRD §4.1 signatures. Geometry only;
// layouts may change freely until first external consumer ships.
struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Rectangle {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

struct Color {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

struct CoordinateTransformResult {
    double screenY = 0.0;
    size_t pageIndex = 0;
    double alpha = 1.0;
};

struct SqueezeRegion {
    std::string id;
    double yStart = 0.0;
    double yEnd = 0.0;
    double alpha = 1.0;
};

struct SqueezeSegment {
    double docYStart = 0.0;
    double docYEnd = 0.0;
    double screenYStart = 0.0;
    double screenYEnd = 0.0;
    double alpha = 1.0;
};

struct BezierSpline {
    std::vector<Point> controlPoints;
};

struct SearchResult {
    std::string entityId;
    std::string entityType;
    int pageIndex = -1;
    std::string snippet;
};

class WorkspaceNode {
  public:
    virtual ~WorkspaceNode() = default;
    virtual const std::string& id() const = 0;
    virtual Rectangle bounds() const = 0;
    virtual std::unique_ptr<WorkspaceNode> clone() const { return nullptr; }
};

class FluidCoreAPI {
  public:
    virtual ~FluidCoreAPI() = default;

    // Document Geometry & Squeeze Layout API (Pure C++ - No Poppler/GTK Dependencies)
    virtual void registerDocumentGeometry(const std::string& docId,
                                          const std::vector<PageGeometry>& pages) = 0;
    virtual CoordinateTransformResult mapDocumentYToScreen(double docY,
                                                           const std::string& docId) const = 0;
    virtual CoordinateTransformResult mapScreenYToDocument(double screenY,
                                                           const std::string& docId) const = 0;
    virtual void setSqueezeRegion(const std::string& docId, double yStart, double yEnd,
                                  double alpha) = 0;
    virtual void setSqueezeRegionWithId(const std::string& docId, const std::string& regionId,
                                        double yStart, double yEnd, double alpha) = 0;
    virtual void removeSqueezeRegion(const std::string& docId, const std::string& regionId) = 0;
    virtual void resetSqueeze(const std::string& docId) = 0;
    virtual std::vector<SqueezeSegment> getSqueezeSegments(const std::string& docId) const = 0;
    virtual double getTotalSqueezedHeight(const std::string& docId) const = 0;

    // Spatial Scene Graph API (UUID-based Identifiers matching SQLite Schema)
    virtual std::string insertNode(std::unique_ptr<WorkspaceNode> node) = 0;
    virtual void updateNodePosition(const std::string& nodeId, double x, double y) = 0;
    virtual void removeNode(const std::string& nodeId) = 0;
    virtual std::vector<WorkspaceNode*>
    queryVisibleNodes(const Rectangle& viewportBounds) const = 0;

    // Pure Geometry Exposure Contract: libfluidcore never receives or returns Cairo/GTK types.
    // The frontend reads geometry (bounds, positions, spline control points via getEdgeGeometry)
    // and performs ALL rendering in the GTK/Cairo layer. Core classes expose no render methods.
    virtual Rectangle getNodeBounds(const std::string& nodeId) const = 0;
    virtual Point getNodePosition(const std::string& nodeId) const = 0;
    virtual Rectangle getWorkspaceBounds() const = 0;

    // Bi-Directional Relational Graph & Live Ink Link API
    virtual std::string createInkLink(const std::string& sourceNodeId,
                                      const std::string& targetNodeId, const Color& color) = 0;
    virtual BezierSpline getEdgeGeometry(const std::string& edgeId) const = 0;
    virtual std::vector<std::string> getConnectedEdges(const std::string& nodeId) const = 0;
    virtual std::vector<std::string> getAllEdges() const = 0;
    virtual bool removeEdge(const std::string& edgeId) = 0;

    // Persistence & Search API
    // TODO(M5): signature-only by design. The .ltproj schema-locking decision is deferred
    // to M5 (ROADMAP §3); implementations must not ship DDL before docs/specs/ltspec.md
    // exists (GOVERNANCE §4).
    virtual void openProject(const std::string& ltprojDirectoryPath) = 0;
    virtual void saveProject() = 0;
    virtual std::vector<SearchResult> executeSearch(const std::string& query) const = 0;
};

} // namespace FluidCore
