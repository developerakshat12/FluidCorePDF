#pragma once

#include "FluidCoreAPI.h"
#include "graph/GraphTopology.h"
#include "workspace/WorkspaceModel.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace FluidCore {

struct DocumentRecord {
    std::string docId;
    std::string filename;
    std::string relativePath; // Normalized forward-slash relative path within bundle
    std::string sha256;
    size_t pageCount = 0;
    size_t fileSizeBytes = 0;
    uint64_t createdAt = 0;

    bool operator==(const DocumentRecord&) const = default;
};

struct ProjectMetadata {
    std::string projectId;
    std::string title = "Untitled Project";
    uint32_t schemaVersion = 1;
    uint64_t createdAt = 0;
    uint64_t updatedAt = 0;

    bool operator==(const ProjectMetadata&) const = default;
};

// Production SQLite WAL Project Storage Manager for .ltproj bundles (TRD §6, §3.3.1).
// Pure C++20 domain model with zero GUI/Cairo/GTK dependencies (ADR-0001).
class ProjectStore {
  public:
    ProjectStore();
    explicit ProjectStore(std::string projectId);
    ~ProjectStore();

    ProjectStore(const ProjectStore&) = delete;
    ProjectStore& operator=(const ProjectStore&) = delete;
    ProjectStore(ProjectStore&&) noexcept;
    ProjectStore& operator=(ProjectStore&&) noexcept;

    // Bundle Lifecycle
    bool openProject(const std::string& bundlePath, std::string* error = nullptr);
    bool saveProject(const WorkspaceModel& model, const GraphTopology& graph,
                     const std::vector<DocumentRecord>& docs = {}, std::string* error = nullptr);
    void closeProject();

    bool isOpen() const { return m_db != nullptr; }
    const std::string& bundlePath() const { return m_bundlePath; }
    const ProjectMetadata& metadata() const { return m_metadata; }
    void setProjectTitle(std::string title) { m_metadata.title = std::move(title); }

    // Rehydration
    bool rehydrate(WorkspaceModel& outModel, GraphTopology& outGraph,
                   std::vector<DocumentRecord>& outDocs, std::string* error = nullptr) const;

    // Document Management
    bool registerDocument(const DocumentRecord& doc, std::string* error = nullptr);
    bool removeDocument(const std::string& docId, std::string* error = nullptr);
    std::vector<DocumentRecord> listDocuments() const;
    std::optional<DocumentRecord> getDocument(const std::string& docId) const;

    // Universal Full-Text Search (FTS5)
    std::vector<SearchResult> executeSearch(const std::string& query) const;

    // Helpers
    static std::string normalizeRelativePath(std::string path);
    static bool ensureBundleStructure(const std::string& bundlePath, std::string* error = nullptr);

  private:
    std::string m_bundlePath;
    ProjectMetadata m_metadata;
    sqlite3* m_db = nullptr;

    bool initSchema(std::string* error);
    bool readMetadataJson(std::string* error);
    bool writeMetadataJson(std::string* error) const;
    void finalizeDb();
};

} // namespace FluidCore
