#include "storage/ProjectStore.h"

#include "workspace/CanvasStrokeNode.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace FluidCore {

namespace {

uint64_t currentTimestampMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

// Simple RAII wrapper for SQLite statements
class SqliteStatement {
  public:
    SqliteStatement(sqlite3* db, const std::string& sql) {
        if (db) {
            sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &m_stmt, nullptr);
        }
    }

    ~SqliteStatement() {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
        }
    }

    sqlite3_stmt* get() const { return m_stmt; }
    bool isValid() const { return m_stmt != nullptr; }

    bool step() { return m_stmt && (sqlite3_step(m_stmt) == SQLITE_ROW); }

    int execute() {
        if (!m_stmt) {
            return SQLITE_ERROR;
        }
        return sqlite3_step(m_stmt);
    }

    void reset() {
        if (m_stmt) {
            sqlite3_reset(m_stmt);
            sqlite3_clear_bindings(m_stmt);
        }
    }

  private:
    sqlite3_stmt* m_stmt = nullptr;
};

std::string escapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string extractJsonField(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return "";
    }
    pos += pattern.size();
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    if (pos >= json.size()) {
        return "";
    }

    if (json[pos] == '"') {
        ++pos;
        std::string val;
        bool escape = false;
        while (pos < json.size()) {
            char c = json[pos++];
            if (escape) {
                val += c;
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                break;
            } else {
                val += c;
            }
        }
        return val;
    } else {
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' &&
               json[end] != ' ' && json[end] != '\t' && json[end] != '\r' && json[end] != '\n') {
            ++end;
        }
        return json.substr(pos, end - pos);
    }
}

} // namespace

std::string ProjectStore::normalizeRelativePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    return path;
}

bool ProjectStore::ensureBundleStructure(const std::string& bundlePath, std::string* error) {
    std::error_code ec;
    std::filesystem::path root(bundlePath);

    std::filesystem::create_directories(root, ec);
    if (ec) {
        if (error)
            *error = "Failed to create bundle root: " + ec.message();
        return false;
    }

    std::filesystem::create_directories(root / "documents", ec);
    std::filesystem::create_directories(root / "assets" / "clips", ec);
    std::filesystem::create_directories(root / "assets" / "images", ec);
    std::filesystem::create_directories(root / "cache" / "thumbnails", ec);
    if (ec) {
        if (error)
            *error = "Failed to create bundle subdirectories: " + ec.message();
        return false;
    }
    return true;
}

ProjectStore::ProjectStore()
    : m_metadata{"project-default", "Untitled Project", 1, currentTimestampMs(),
                 currentTimestampMs()} {}

ProjectStore::ProjectStore(std::string projectId)
    : m_metadata{std::move(projectId), "Untitled Project", 1, currentTimestampMs(),
                 currentTimestampMs()} {}

ProjectStore::~ProjectStore() {
    closeProject();
}

ProjectStore::ProjectStore(ProjectStore&& other) noexcept
    : m_bundlePath(std::move(other.m_bundlePath)), m_metadata(std::move(other.m_metadata)),
      m_db(other.m_db) {
    other.m_db = nullptr;
}

ProjectStore& ProjectStore::operator=(ProjectStore&& other) noexcept {
    if (this != &other) {
        closeProject();
        m_bundlePath = std::move(other.m_bundlePath);
        m_metadata = std::move(other.m_metadata);
        m_db = other.m_db;
        other.m_db = nullptr;
    }
    return *this;
}

void ProjectStore::finalizeDb() {
    if (m_db) {
        sqlite3_exec(m_db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

void ProjectStore::closeProject() {
    finalizeDb();
    m_bundlePath.clear();
}

bool ProjectStore::initSchema(std::string* error) {
    if (!m_db) {
        if (error)
            *error = "Database is not open";
        return false;
    }

    const char* ddl = R"SQL(
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = NORMAL;
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS projects (
            project_id TEXT PRIMARY KEY NOT NULL,
            title TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            schema_version INTEGER NOT NULL DEFAULT 1
        );

        CREATE TABLE IF NOT EXISTS documents (
            doc_id TEXT PRIMARY KEY NOT NULL,
            project_id TEXT NOT NULL REFERENCES projects(project_id) ON DELETE CASCADE,
            filename TEXT NOT NULL,
            file_path_relative TEXT NOT NULL,
            file_sha256 TEXT NOT NULL,
            page_count INTEGER NOT NULL,
            file_size_bytes INTEGER NOT NULL,
            created_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS workspace_nodes (
            node_id TEXT PRIMARY KEY NOT NULL,
            project_id TEXT NOT NULL REFERENCES projects(project_id) ON DELETE CASCADE,
            node_type TEXT NOT NULL CHECK(node_type IN ('TEXT_EXCERPT', 'IMAGE_CLIP', 'STICKY_NOTE', 'TEXT_BOX', 'STACK_HEADER')),
            pos_x REAL NOT NULL,
            pos_y REAL NOT NULL,
            width REAL NOT NULL,
            height REAL NOT NULL,
            z_index INTEGER NOT NULL DEFAULT 0,
            parent_stack_id TEXT REFERENCES workspace_nodes(node_id) ON DELETE SET NULL,
            title TEXT,
            is_collapsed INTEGER DEFAULT 0,
            color INTEGER DEFAULT 4294967295,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS source_anchors (
            anchor_id TEXT PRIMARY KEY NOT NULL,
            node_id TEXT NOT NULL REFERENCES workspace_nodes(node_id) ON DELETE CASCADE,
            doc_id TEXT NOT NULL REFERENCES documents(doc_id) ON DELETE CASCADE,
            page_index INTEGER NOT NULL,
            rect_x0 REAL NOT NULL,
            rect_y0 REAL NOT NULL,
            rect_x1 REAL NOT NULL,
            rect_y1 REAL NOT NULL,
            raw_text_content TEXT,
            highlight_color INTEGER DEFAULT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_source_anchors_node ON source_anchors(node_id);
        CREATE INDEX IF NOT EXISTS idx_source_anchors_doc ON source_anchors(doc_id, page_index);

        CREATE TABLE IF NOT EXISTS graph_edges (
            edge_id TEXT PRIMARY KEY NOT NULL,
            project_id TEXT NOT NULL REFERENCES projects(project_id) ON DELETE CASCADE,
            source_node_id TEXT NOT NULL REFERENCES workspace_nodes(node_id) ON DELETE CASCADE,
            target_node_id TEXT NOT NULL REFERENCES workspace_nodes(node_id) ON DELETE CASCADE,
            edge_type TEXT NOT NULL CHECK(edge_type IN ('INK_LINK', 'MANUAL_LINK', 'HIERARCHY')),
            edge_kind TEXT NOT NULL DEFAULT 'GENERIC',
            direction INTEGER NOT NULL DEFAULT 0,
            stroke_geometry_blob BLOB,
            color INTEGER NOT NULL,
            stroke_width REAL NOT NULL DEFAULT 2.0,
            arrow_style INTEGER NOT NULL DEFAULT 0,
            label TEXT,
            created_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_graph_edges_source ON graph_edges(source_node_id);
        CREATE INDEX IF NOT EXISTS idx_graph_edges_target ON graph_edges(target_node_id);

        CREATE TABLE IF NOT EXISTS ink_strokes (
            stroke_id TEXT PRIMARY KEY NOT NULL,
            project_id TEXT NOT NULL REFERENCES projects(project_id) ON DELETE CASCADE,
            container_type TEXT NOT NULL CHECK(container_type IN ('DOCUMENT', 'WORKSPACE', 'NODE')),
            container_ref_id TEXT NOT NULL,
            page_index INTEGER DEFAULT NULL,
            bounding_box_blob BLOB NOT NULL,
            points_blob BLOB NOT NULL,
            tool_type TEXT NOT NULL,
            color INTEGER NOT NULL,
            base_width REAL NOT NULL,
            created_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS tags (
            tag_id TEXT PRIMARY KEY NOT NULL,
            project_id TEXT NOT NULL REFERENCES projects(project_id) ON DELETE CASCADE,
            tag_name TEXT NOT NULL,
            tag_color INTEGER NOT NULL DEFAULT 0,
            UNIQUE(project_id, tag_name)
        );

        CREATE TABLE IF NOT EXISTS entity_tags (
            tag_id TEXT NOT NULL REFERENCES tags(tag_id) ON DELETE CASCADE,
            entity_id TEXT NOT NULL,
            entity_type TEXT NOT NULL,
            PRIMARY KEY(tag_id, entity_id)
        );

        CREATE VIRTUAL TABLE IF NOT EXISTS fts_universal_index USING fts5(
            entity_id UNINDEXED,
            entity_type UNINDEXED,
            page_index UNINDEXED,
            text_content,
            tokenize = 'unicode61 remove_diacritics 2'
        );
    )SQL";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, ddl, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = (errMsg ? std::string(errMsg) : "Failed to execute DDL");
        }
        if (errMsg)
            sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool ProjectStore::readMetadataJson(std::string* error) {
    std::filesystem::path metaPath = std::filesystem::path(m_bundlePath) / "metadata.json";
    if (!std::filesystem::exists(metaPath)) {
        return true; // Use defaults
    }

    std::ifstream file(metaPath);
    if (!file) {
        if (error)
            *error = "Cannot open metadata.json";
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string pId = extractJsonField(content, "project_id");
    if (!pId.empty())
        m_metadata.projectId = pId;

    std::string title = extractJsonField(content, "title");
    if (!title.empty())
        m_metadata.title = title;

    std::string schemaVerStr = extractJsonField(content, "schema_version");
    if (!schemaVerStr.empty()) {
        try {
            m_metadata.schemaVersion = static_cast<uint32_t>(std::stoul(schemaVerStr));
        } catch (...) {
        }
    }

    std::string cAtStr = extractJsonField(content, "created_at");
    if (!cAtStr.empty()) {
        try {
            m_metadata.createdAt = std::stoull(cAtStr);
        } catch (...) {
        }
    }

    std::string uAtStr = extractJsonField(content, "updated_at");
    if (!uAtStr.empty()) {
        try {
            m_metadata.updatedAt = std::stoull(uAtStr);
        } catch (...) {
        }
    }
    return true;
}

bool ProjectStore::writeMetadataJson(std::string* error) const {
    std::filesystem::path metaPath = std::filesystem::path(m_bundlePath) / "metadata.json";
    static std::atomic<uint64_t> s_counter{0};
    std::string tmpPath = metaPath.string() + ".tmp." + std::to_string(++s_counter);

    std::ofstream file(tmpPath, std::ios::trunc);
    if (!file) {
        if (error)
            *error = "Cannot open temporary metadata.json for writing";
        return false;
    }

    file << "{\n";
    file << "  \"project_id\": \"" << escapeJsonString(m_metadata.projectId) << "\",\n";
    file << "  \"title\": \"" << escapeJsonString(m_metadata.title) << "\",\n";
    file << "  \"schema_version\": " << m_metadata.schemaVersion << ",\n";
    file << "  \"created_at\": " << m_metadata.createdAt << ",\n";
    file << "  \"updated_at\": " << m_metadata.updatedAt << "\n";
    file << "}\n";
    file.flush();
    file.close();

#if !defined(_WIN32)
    int fd = ::open(tmpPath.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif

    std::error_code ec;
    std::filesystem::rename(tmpPath, metaPath, ec);
    if (ec) {
        if (error)
            *error = "Failed to rename metadata.json: " + ec.message();
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

#if !defined(_WIN32)
    int dirFd = ::open(m_bundlePath.c_str(), O_RDONLY | O_DIRECTORY);
    if (dirFd >= 0) {
        ::fsync(dirFd);
        ::close(dirFd);
    }
#endif
    return true;
}

bool ProjectStore::openProject(const std::string& bundlePath, std::string* error) {
    closeProject();

    if (!ensureBundleStructure(bundlePath, error)) {
        return false;
    }

    m_bundlePath = bundlePath;
    readMetadataJson(nullptr);

    std::filesystem::path dbPath = std::filesystem::path(bundlePath) / "project.db";
    int rc = sqlite3_open_v2(dbPath.string().c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = sqlite3_errmsg(m_db);
        }
        finalizeDb();
        return false;
    }

    if (!initSchema(error)) {
        finalizeDb();
        return false;
    }

    // Read or initialize project row
    SqliteStatement selectProj(
        m_db,
        "SELECT project_id, title, schema_version, created_at, updated_at FROM projects LIMIT 1;");
    if (selectProj.isValid() && selectProj.step()) {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(selectProj.get(), 0));
        if (p)
            m_metadata.projectId = p;
        const char* t = reinterpret_cast<const char*>(sqlite3_column_text(selectProj.get(), 1));
        if (t)
            m_metadata.title = t;
        m_metadata.schemaVersion = static_cast<uint32_t>(sqlite3_column_int(selectProj.get(), 2));
        m_metadata.createdAt = static_cast<uint64_t>(sqlite3_column_int64(selectProj.get(), 3));
        m_metadata.updatedAt = static_cast<uint64_t>(sqlite3_column_int64(selectProj.get(), 4));
    } else {
        SqliteStatement insertProj(m_db, "INSERT INTO projects (project_id, title, schema_version, "
                                         "created_at, updated_at) VALUES (?, ?, ?, ?, ?);");
        if (insertProj.isValid()) {
            sqlite3_bind_text(insertProj.get(), 1, m_metadata.projectId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertProj.get(), 2, m_metadata.title.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertProj.get(), 3, static_cast<int>(m_metadata.schemaVersion));
            sqlite3_bind_int64(insertProj.get(), 4,
                               static_cast<sqlite3_int64>(m_metadata.createdAt));
            sqlite3_bind_int64(insertProj.get(), 5,
                               static_cast<sqlite3_int64>(m_metadata.updatedAt));
            insertProj.execute();
        }
    }

    // Always ensure metadata.json is written to disk upon open
    writeMetadataJson(nullptr);
    return true;
}

bool ProjectStore::registerDocument(const DocumentRecord& doc, std::string* error) {
    if (!m_db) {
        if (error)
            *error = "Database not open";
        return false;
    }

    SqliteStatement stmt(m_db,
                         "INSERT INTO documents (doc_id, project_id, filename, file_path_relative, "
                         "file_sha256, page_count, file_size_bytes, created_at) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                         "ON CONFLICT(doc_id) DO UPDATE SET "
                         "filename = excluded.filename, "
                         "file_path_relative = excluded.file_path_relative, "
                         "file_sha256 = excluded.file_sha256, "
                         "page_count = excluded.page_count, "
                         "file_size_bytes = excluded.file_size_bytes;");

    if (!stmt.isValid()) {
        if (error)
            *error = sqlite3_errmsg(m_db);
        return false;
    }

    std::string normPath = normalizeRelativePath(doc.relativePath);
    sqlite3_bind_text(stmt.get(), 1, doc.docId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, m_metadata.projectId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 3, doc.filename.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 4, normPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 5, doc.sha256.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt.get(), 6, static_cast<sqlite3_int64>(doc.pageCount));
    sqlite3_bind_int64(stmt.get(), 7, static_cast<sqlite3_int64>(doc.fileSizeBytes));
    sqlite3_bind_int64(
        stmt.get(), 8,
        static_cast<sqlite3_int64>(doc.createdAt ? doc.createdAt : currentTimestampMs()));

    int rc = stmt.execute();
    if (rc != SQLITE_DONE) {
        if (error)
            *error = sqlite3_errmsg(m_db);
        return false;
    }
    return true;
}

bool ProjectStore::removeDocument(const std::string& docId, std::string* error) {
    if (!m_db) {
        if (error)
            *error = "Database not open";
        return false;
    }

    SqliteStatement stmt(m_db, "DELETE FROM documents WHERE doc_id = ?;");
    if (!stmt.isValid()) {
        if (error)
            *error = sqlite3_errmsg(m_db);
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, docId.c_str(), -1, SQLITE_STATIC);
    int rc = stmt.execute();
    if (rc != SQLITE_DONE) {
        if (error)
            *error = sqlite3_errmsg(m_db);
        return false;
    }

    // Clean up companion filesystem artifacts to prevent orphaned file accumulation
    if (!m_bundlePath.empty()) {
        std::error_code ec;
        std::filesystem::path docDir = std::filesystem::path(m_bundlePath) / "documents";
        std::filesystem::remove(docDir / (docId + ".xopp"), ec);
        std::filesystem::remove_all(
            std::filesystem::path(m_bundlePath) / "cache" / "thumbnails" / docId, ec);
    }
    return true;
}

std::vector<DocumentRecord> ProjectStore::listDocuments() const {
    std::vector<DocumentRecord> list;
    if (!m_db)
        return list;

    SqliteStatement stmt(m_db,
                         "SELECT doc_id, filename, file_path_relative, file_sha256, page_count, "
                         "file_size_bytes, created_at FROM documents WHERE project_id = ?;");
    if (!stmt.isValid())
        return list;

    sqlite3_bind_text(stmt.get(), 1, m_metadata.projectId.c_str(), -1, SQLITE_STATIC);
    while (stmt.step()) {
        DocumentRecord rec;
        rec.docId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        rec.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        rec.relativePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
        rec.sha256 = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
        rec.pageCount = static_cast<size_t>(sqlite3_column_int64(stmt.get(), 4));
        rec.fileSizeBytes = static_cast<size_t>(sqlite3_column_int64(stmt.get(), 5));
        rec.createdAt = static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 6));
        list.push_back(std::move(rec));
    }
    return list;
}

std::optional<DocumentRecord> ProjectStore::getDocument(const std::string& docId) const {
    if (!m_db)
        return std::nullopt;

    SqliteStatement stmt(m_db,
                         "SELECT doc_id, filename, file_path_relative, file_sha256, page_count, "
                         "file_size_bytes, created_at FROM documents WHERE doc_id = ?;");
    if (!stmt.isValid())
        return std::nullopt;

    sqlite3_bind_text(stmt.get(), 1, docId.c_str(), -1, SQLITE_STATIC);
    if (stmt.step()) {
        DocumentRecord rec;
        rec.docId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        rec.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        rec.relativePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
        rec.sha256 = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
        rec.pageCount = static_cast<size_t>(sqlite3_column_int64(stmt.get(), 4));
        rec.fileSizeBytes = static_cast<size_t>(sqlite3_column_int64(stmt.get(), 5));
        rec.createdAt = static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 6));
        return rec;
    }
    return std::nullopt;
}

namespace {

void serializeNodeRecursive(sqlite3* db, const std::string& projectId, const WorkspaceNode* node,
                            const std::string& parentStackId, int zIndex,
                            SqliteStatement& insertNodeStmt, SqliteStatement& insertAnchorStmt,
                            SqliteStatement& insertTagStmt, SqliteStatement& insertEntityTagStmt,
                            SqliteStatement& insertFtsStmt) {
    if (!node)
        return;

    const std::string& nodeId = node->id();
    Rectangle bounds = node->bounds();
    uint64_t now = currentTimestampMs();

    if (const auto* card = dynamic_cast<const ExcerptCardNode*>(node)) {
        insertNodeStmt.reset();
        sqlite3_bind_text(insertNodeStmt.get(), 1, nodeId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertNodeStmt.get(), 2, projectId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertNodeStmt.get(), 3,
                          card->isImageExcerpt() ? "IMAGE_CLIP" : "TEXT_EXCERPT", -1,
                          SQLITE_STATIC);
        sqlite3_bind_double(insertNodeStmt.get(), 4, bounds.x);
        sqlite3_bind_double(insertNodeStmt.get(), 5, bounds.y);
        sqlite3_bind_double(insertNodeStmt.get(), 6, bounds.w);
        sqlite3_bind_double(insertNodeStmt.get(), 7, bounds.h);
        sqlite3_bind_int(insertNodeStmt.get(), 8, zIndex);
        if (!parentStackId.empty()) {
            sqlite3_bind_text(insertNodeStmt.get(), 9, parentStackId.c_str(), -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(insertNodeStmt.get(), 9);
        }
        sqlite3_bind_null(insertNodeStmt.get(), 10);   // title
        sqlite3_bind_int(insertNodeStmt.get(), 11, 0); // is_collapsed
        Color c = card->color();
        uint32_t colorU32 = (static_cast<uint32_t>(c.r) << 24) |
                            (static_cast<uint32_t>(c.g) << 16) | (static_cast<uint32_t>(c.b) << 8) |
                            static_cast<uint32_t>(c.a);
        sqlite3_bind_int64(insertNodeStmt.get(), 12, colorU32);
        sqlite3_bind_int64(insertNodeStmt.get(), 13,
                           static_cast<sqlite3_int64>(
                               card->creationTimestamp() ? card->creationTimestamp() : now));
        sqlite3_bind_int64(insertNodeStmt.get(), 14, static_cast<sqlite3_int64>(now));
        insertNodeStmt.execute();

        // Source Anchor
        insertAnchorStmt.reset();
        std::string anchorId = "anchor_" + nodeId;
        Rectangle normRect = card->sourceNormalizedRect();
        sqlite3_bind_text(insertAnchorStmt.get(), 1, anchorId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertAnchorStmt.get(), 2, nodeId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertAnchorStmt.get(), 3, card->sourceDocId().c_str(), -1,
                          SQLITE_STATIC);
        sqlite3_bind_int64(insertAnchorStmt.get(), 4,
                           static_cast<sqlite3_int64>(card->sourcePageNo()));
        sqlite3_bind_double(insertAnchorStmt.get(), 5, normRect.x);
        sqlite3_bind_double(insertAnchorStmt.get(), 6, normRect.y);
        sqlite3_bind_double(insertAnchorStmt.get(), 7, normRect.w);
        sqlite3_bind_double(insertAnchorStmt.get(), 8, normRect.h);
        if (!card->textSnippet().empty()) {
            sqlite3_bind_text(insertAnchorStmt.get(), 9, card->textSnippet().c_str(), -1,
                              SQLITE_STATIC);
        } else {
            sqlite3_bind_null(insertAnchorStmt.get(), 9);
        }
        sqlite3_bind_null(insertAnchorStmt.get(), 10);
        insertAnchorStmt.execute();

        // Tags
        for (const std::string& tag : card->tags()) {
            insertTagStmt.reset();
            std::string tagId = "tag_" + tag;
            sqlite3_bind_text(insertTagStmt.get(), 1, tagId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertTagStmt.get(), 2, projectId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertTagStmt.get(), 3, tag.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertTagStmt.get(), 4, 0);
            insertTagStmt.execute();

            insertEntityTagStmt.reset();
            sqlite3_bind_text(insertEntityTagStmt.get(), 1, tagId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertEntityTagStmt.get(), 2, nodeId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertEntityTagStmt.get(), 3, "TEXT_EXCERPT", -1, SQLITE_STATIC);
            insertEntityTagStmt.execute();
        }

        // FTS Index
        if (!card->textSnippet().empty()) {
            insertFtsStmt.reset();
            sqlite3_bind_text(insertFtsStmt.get(), 1, nodeId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertFtsStmt.get(), 2, "TEXT_EXCERPT", -1, SQLITE_STATIC);
            sqlite3_bind_int64(insertFtsStmt.get(), 3,
                               static_cast<sqlite3_int64>(card->sourcePageNo()));
            sqlite3_bind_text(insertFtsStmt.get(), 4, card->textSnippet().c_str(), -1,
                              SQLITE_STATIC);
            insertFtsStmt.execute();
        }
    } else if (const auto* stack = dynamic_cast<const CardStackNode*>(node)) {
        insertNodeStmt.reset();
        sqlite3_bind_text(insertNodeStmt.get(), 1, nodeId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertNodeStmt.get(), 2, projectId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertNodeStmt.get(), 3, "STACK_HEADER", -1, SQLITE_STATIC);
        sqlite3_bind_double(insertNodeStmt.get(), 4, bounds.x);
        sqlite3_bind_double(insertNodeStmt.get(), 5, bounds.y);
        sqlite3_bind_double(insertNodeStmt.get(), 6, bounds.w);
        sqlite3_bind_double(insertNodeStmt.get(), 7, bounds.h);
        sqlite3_bind_int(insertNodeStmt.get(), 8, zIndex);
        if (!parentStackId.empty()) {
            sqlite3_bind_text(insertNodeStmt.get(), 9, parentStackId.c_str(), -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(insertNodeStmt.get(), 9);
        }
        if (!stack->title().empty()) {
            sqlite3_bind_text(insertNodeStmt.get(), 10, stack->title().c_str(), -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(insertNodeStmt.get(), 10);
        }
        sqlite3_bind_int(insertNodeStmt.get(), 11, stack->isCollapsed() ? 1 : 0);
        sqlite3_bind_int64(insertNodeStmt.get(), 12, 4294967295ULL);
        sqlite3_bind_int64(insertNodeStmt.get(), 13, static_cast<sqlite3_int64>(now));
        sqlite3_bind_int64(insertNodeStmt.get(), 14, static_cast<sqlite3_int64>(now));
        insertNodeStmt.execute();

        // Stack Tags
        for (const std::string& tag : stack->tags()) {
            insertTagStmt.reset();
            std::string tagId = "tag_" + tag;
            sqlite3_bind_text(insertTagStmt.get(), 1, tagId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertTagStmt.get(), 2, projectId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertTagStmt.get(), 3, tag.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertTagStmt.get(), 4, 0);
            insertTagStmt.execute();

            insertEntityTagStmt.reset();
            sqlite3_bind_text(insertEntityTagStmt.get(), 1, tagId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertEntityTagStmt.get(), 2, nodeId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertEntityTagStmt.get(), 3, "STACK_HEADER", -1, SQLITE_STATIC);
            insertEntityTagStmt.execute();
        }

        // Stack FTS
        if (!stack->title().empty()) {
            insertFtsStmt.reset();
            sqlite3_bind_text(insertFtsStmt.get(), 1, nodeId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertFtsStmt.get(), 2, "STACK_HEADER", -1, SQLITE_STATIC);
            sqlite3_bind_null(insertFtsStmt.get(), 3);
            sqlite3_bind_text(insertFtsStmt.get(), 4, stack->title().c_str(), -1, SQLITE_STATIC);
            insertFtsStmt.execute();
        }

        // Recurse on stack children
        int childZ = zIndex + 1;
        for (const auto& child : stack->children()) {
            serializeNodeRecursive(db, projectId, child.get(), nodeId, childZ++, insertNodeStmt,
                                   insertAnchorStmt, insertTagStmt, insertEntityTagStmt,
                                   insertFtsStmt);
        }
    } else {
        // Generic / Free note
        insertNodeStmt.reset();
        sqlite3_bind_text(insertNodeStmt.get(), 1, nodeId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertNodeStmt.get(), 2, projectId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertNodeStmt.get(), 3, "STICKY_NOTE", -1, SQLITE_STATIC);
        sqlite3_bind_double(insertNodeStmt.get(), 4, bounds.x);
        sqlite3_bind_double(insertNodeStmt.get(), 5, bounds.y);
        sqlite3_bind_double(insertNodeStmt.get(), 6, bounds.w);
        sqlite3_bind_double(insertNodeStmt.get(), 7, bounds.h);
        sqlite3_bind_int(insertNodeStmt.get(), 8, zIndex);
        if (!parentStackId.empty()) {
            sqlite3_bind_text(insertNodeStmt.get(), 9, parentStackId.c_str(), -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(insertNodeStmt.get(), 9);
        }
        sqlite3_bind_null(insertNodeStmt.get(), 10);
        sqlite3_bind_int(insertNodeStmt.get(), 11, 0);
        sqlite3_bind_int64(insertNodeStmt.get(), 12, 4294967295ULL);
        sqlite3_bind_int64(insertNodeStmt.get(), 13, static_cast<sqlite3_int64>(now));
        sqlite3_bind_int64(insertNodeStmt.get(), 14, static_cast<sqlite3_int64>(now));
        insertNodeStmt.execute();
    }
}

} // namespace

bool ProjectStore::saveProject(const WorkspaceModel& model, const GraphTopology& graph,
                               const std::vector<DocumentRecord>& docs, std::string* error) {
    if (!m_db) {
        if (error)
            *error = "Database not open";
        return false;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (error)
            *error = (errMsg ? std::string(errMsg) : "Failed to begin transaction");
        if (errMsg)
            sqlite3_free(errMsg);
        return false;
    }

    m_metadata.updatedAt = currentTimestampMs();

    // 1. Update project metadata
    SqliteStatement updateProj(m_db, "INSERT INTO projects (project_id, title, schema_version, "
                                     "created_at, updated_at) VALUES (?, ?, ?, ?, ?) "
                                     "ON CONFLICT(project_id) DO UPDATE SET title = "
                                     "excluded.title, updated_at = excluded.updated_at;");
    if (updateProj.isValid()) {
        sqlite3_bind_text(updateProj.get(), 1, m_metadata.projectId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(updateProj.get(), 2, m_metadata.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(updateProj.get(), 3, static_cast<int>(m_metadata.schemaVersion));
        sqlite3_bind_int64(updateProj.get(), 4, static_cast<sqlite3_int64>(m_metadata.createdAt));
        sqlite3_bind_int64(updateProj.get(), 5, static_cast<sqlite3_int64>(m_metadata.updatedAt));
        updateProj.execute();
    }

    // 2. Documents
    for (const auto& doc : docs) {
        registerDocument(doc, nullptr);
    }

    // 3. Clear existing workspace scene graph rows (foreign keys clean up anchors, entity_tags)
    sqlite3_exec(m_db, "DELETE FROM workspace_nodes;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "DELETE FROM graph_edges;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "DELETE FROM fts_universal_index;", nullptr, nullptr, nullptr);

    SqliteStatement insertNodeStmt(
        m_db,
        "INSERT INTO workspace_nodes (node_id, project_id, node_type, pos_x, pos_y, width, height, "
        "z_index, parent_stack_id, title, is_collapsed, color, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    SqliteStatement insertAnchorStmt(
        m_db, "INSERT INTO source_anchors (anchor_id, node_id, doc_id, page_index, rect_x0, "
              "rect_y0, rect_x1, rect_y1, raw_text_content, highlight_color) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    SqliteStatement insertTagStmt(m_db, "INSERT OR IGNORE INTO tags (tag_id, project_id, tag_name, "
                                        "tag_color) VALUES (?, ?, ?, ?);");

    SqliteStatement insertEntityTagStmt(
        m_db,
        "INSERT OR IGNORE INTO entity_tags (tag_id, entity_id, entity_type) VALUES (?, ?, ?);");

    SqliteStatement insertFtsStmt(m_db, "INSERT INTO fts_universal_index (entity_id, entity_type, "
                                        "page_index, text_content) VALUES (?, ?, ?, ?);");

    // Serialize top-level nodes and their hierarchy
    int rootZ = 1;
    for (const std::string& nodeId : model.allNodeIds()) {
        const WorkspaceNode* node = model.find(nodeId);
        if (node) {
            serializeNodeRecursive(m_db, m_metadata.projectId, node, "", rootZ++, insertNodeStmt,
                                   insertAnchorStmt, insertTagStmt, insertEntityTagStmt,
                                   insertFtsStmt);
        }
    }

    // 4. Graph Edges
    SqliteStatement insertEdgeStmt(
        m_db,
        "INSERT INTO graph_edges (edge_id, project_id, source_node_id, target_node_id, edge_type, "
        "edge_kind, direction, color, stroke_width, arrow_style, label, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    for (const std::string& edgeId : graph.allEdgeIds()) {
        auto edgeOpt = graph.findEdge(edgeId);
        if (edgeOpt) {
            const GraphEdge& edge = *edgeOpt;
            insertEdgeStmt.reset();
            sqlite3_bind_text(insertEdgeStmt.get(), 1, edge.id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertEdgeStmt.get(), 2, m_metadata.projectId.c_str(), -1,
                              SQLITE_STATIC);
            sqlite3_bind_text(insertEdgeStmt.get(), 3, edge.sourceNodeId.c_str(), -1,
                              SQLITE_STATIC);
            sqlite3_bind_text(insertEdgeStmt.get(), 4, edge.targetNodeId.c_str(), -1,
                              SQLITE_STATIC);
            sqlite3_bind_text(insertEdgeStmt.get(), 5, "INK_LINK", -1, SQLITE_STATIC);
            sqlite3_bind_text(insertEdgeStmt.get(), 6, "GENERIC", -1, SQLITE_STATIC);
            sqlite3_bind_int(insertEdgeStmt.get(), 7, static_cast<int>(edge.direction));
            uint32_t colorU32 = (static_cast<uint32_t>(edge.color.r) << 24) |
                                (static_cast<uint32_t>(edge.color.g) << 16) |
                                (static_cast<uint32_t>(edge.color.b) << 8) |
                                static_cast<uint32_t>(edge.color.a);
            sqlite3_bind_int64(insertEdgeStmt.get(), 8, colorU32);
            sqlite3_bind_double(insertEdgeStmt.get(), 9, edge.strokeWidth);
            sqlite3_bind_int(insertEdgeStmt.get(), 10, static_cast<int>(edge.arrowStyle));
            if (!edge.label.empty()) {
                sqlite3_bind_text(insertEdgeStmt.get(), 11, edge.label.c_str(), -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(insertEdgeStmt.get(), 11);
            }
            sqlite3_bind_int64(insertEdgeStmt.get(), 12,
                               static_cast<sqlite3_int64>(m_metadata.updatedAt));
            insertEdgeStmt.execute();
        }
    }

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (error)
            *error = (errMsg ? std::string(errMsg) : "Failed to commit transaction");
        if (errMsg)
            sqlite3_free(errMsg);
        return false;
    }

    // Write atomic metadata.json
    writeMetadataJson(nullptr);
    return true;
}

bool ProjectStore::rehydrate(WorkspaceModel& outModel, GraphTopology& outGraph,
                             std::vector<DocumentRecord>& outDocs, std::string* error) const {
    if (!m_db) {
        if (error)
            *error = "Database not open";
        return false;
    }

    // 1. Documents
    outDocs = listDocuments();

    // 2. Load tags per entity
    std::unordered_map<std::string, std::vector<std::string>> entityTags;
    SqliteStatement tagStmt(
        m_db,
        "SELECT et.entity_id, t.tag_name FROM entity_tags et JOIN tags t ON et.tag_id = t.tag_id;");
    if (tagStmt.isValid()) {
        while (tagStmt.step()) {
            std::string entityId =
                reinterpret_cast<const char*>(sqlite3_column_text(tagStmt.get(), 0));
            std::string tagName =
                reinterpret_cast<const char*>(sqlite3_column_text(tagStmt.get(), 1));
            entityTags[entityId].push_back(tagName);
        }
    }

    // 3. Load source anchors
    struct AnchorData {
        std::string docId;
        size_t pageIndex = 0;
        Rectangle normRect{0.0, 0.0, 1.0, 1.0};
        std::string textSnippet;
    };
    std::unordered_map<std::string, AnchorData> anchors;

    SqliteStatement anchorStmt(m_db, "SELECT node_id, doc_id, page_index, rect_x0, rect_y0, "
                                     "rect_x1, rect_y1, raw_text_content FROM source_anchors;");
    if (anchorStmt.isValid()) {
        while (anchorStmt.step()) {
            std::string nodeId =
                reinterpret_cast<const char*>(sqlite3_column_text(anchorStmt.get(), 0));
            AnchorData ad;
            ad.docId = reinterpret_cast<const char*>(sqlite3_column_text(anchorStmt.get(), 1));
            ad.pageIndex = static_cast<size_t>(sqlite3_column_int64(anchorStmt.get(), 2));
            ad.normRect.x = sqlite3_column_double(anchorStmt.get(), 3);
            ad.normRect.y = sqlite3_column_double(anchorStmt.get(), 4);
            ad.normRect.w = sqlite3_column_double(anchorStmt.get(), 5);
            ad.normRect.h = sqlite3_column_double(anchorStmt.get(), 6);
            const char* txt =
                reinterpret_cast<const char*>(sqlite3_column_text(anchorStmt.get(), 7));
            if (txt)
                ad.textSnippet = txt;
            anchors[nodeId] = std::move(ad);
        }
    }

    // 4. Load all workspace node records
    struct NodeRecord {
        std::string nodeId;
        std::string nodeType;
        Rectangle bounds{0.0, 0.0, 0.0, 0.0};
        std::string parentStackId;
        std::string title;
        bool isCollapsed = false;
        Color color{255, 255, 255, 255};
        uint64_t createdAt = 0;
    };

    std::vector<NodeRecord> allNodes;
    std::unordered_map<std::string, std::vector<std::string>> stackChildrenIds;

    SqliteStatement nodeStmt(
        m_db, "SELECT node_id, node_type, pos_x, pos_y, width, height, parent_stack_id, title, "
              "is_collapsed, color, created_at FROM workspace_nodes ORDER BY z_index ASC;");
    if (nodeStmt.isValid()) {
        while (nodeStmt.step()) {
            NodeRecord nr;
            nr.nodeId = reinterpret_cast<const char*>(sqlite3_column_text(nodeStmt.get(), 0));
            nr.nodeType = reinterpret_cast<const char*>(sqlite3_column_text(nodeStmt.get(), 1));
            nr.bounds.x = sqlite3_column_double(nodeStmt.get(), 2);
            nr.bounds.y = sqlite3_column_double(nodeStmt.get(), 3);
            nr.bounds.w = sqlite3_column_double(nodeStmt.get(), 4);
            nr.bounds.h = sqlite3_column_double(nodeStmt.get(), 5);
            const char* parent =
                reinterpret_cast<const char*>(sqlite3_column_text(nodeStmt.get(), 6));
            if (parent)
                nr.parentStackId = parent;
            const char* tit = reinterpret_cast<const char*>(sqlite3_column_text(nodeStmt.get(), 7));
            if (tit)
                nr.title = tit;
            nr.isCollapsed = (sqlite3_column_int(nodeStmt.get(), 8) != 0);
            uint32_t colorU32 = static_cast<uint32_t>(sqlite3_column_int64(nodeStmt.get(), 9));
            nr.color.r = static_cast<unsigned char>((colorU32 >> 24) & 0xFF);
            nr.color.g = static_cast<unsigned char>((colorU32 >> 16) & 0xFF);
            nr.color.b = static_cast<unsigned char>((colorU32 >> 8) & 0xFF);
            nr.color.a = static_cast<unsigned char>(colorU32 & 0xFF);
            nr.createdAt = static_cast<uint64_t>(sqlite3_column_int64(nodeStmt.get(), 10));

            if (!nr.parentStackId.empty()) {
                stackChildrenIds[nr.parentStackId].push_back(nr.nodeId);
            }
            allNodes.push_back(std::move(nr));
        }
    }

    // Build node map
    std::unordered_map<std::string, NodeRecord> nodeMap;
    for (const auto& nr : allNodes) {
        nodeMap[nr.nodeId] = nr;
    }

    // Recursive node factory
    auto instantiateNode = [&](auto& self,
                               const std::string& nId) -> std::unique_ptr<WorkspaceNode> {
        auto it = nodeMap.find(nId);
        if (it == nodeMap.end())
            return nullptr;
        const NodeRecord& nr = it->second;

        if (nr.nodeType == "STACK_HEADER") {
            auto stack =
                std::make_unique<CardStackNode>(nr.nodeId, nr.bounds, nr.title, nr.isCollapsed);
            auto tagIt = entityTags.find(nr.nodeId);
            if (tagIt != entityTags.end()) {
                stack->setTags(tagIt->second);
            }
            auto childrenIt = stackChildrenIds.find(nr.nodeId);
            if (childrenIt != stackChildrenIds.end()) {
                for (const std::string& childId : childrenIt->second) {
                    auto childNode = self(self, childId);
                    if (childNode) {
                        stack->addChild(std::move(childNode));
                    }
                }
            }
            stack->recalculateLayout();
            return stack;
        } else {
            // Excerpt card
            std::string docId;
            size_t pageNo = 0;
            Rectangle normRect{0.0, 0.0, 1.0, 1.0};
            std::string snippet;

            auto aIt = anchors.find(nr.nodeId);
            if (aIt != anchors.end()) {
                docId = aIt->second.docId;
                pageNo = aIt->second.pageIndex;
                normRect = aIt->second.normRect;
                snippet = aIt->second.textSnippet;
            }

            bool isImage = (nr.nodeType == "IMAGE_CLIP");
            auto card =
                std::make_unique<ExcerptCardNode>(nr.nodeId, nr.bounds, docId, pageNo, normRect,
                                                  snippet, isImage, nr.color, nr.createdAt);
            auto tagIt = entityTags.find(nr.nodeId);
            if (tagIt != entityTags.end()) {
                card->setTags(tagIt->second);
            }
            return card;
        }
    };

    // Insert only root nodes into outModel
    for (const auto& nr : allNodes) {
        if (nr.parentStackId.empty()) {
            auto nodePtr = instantiateNode(instantiateNode, nr.nodeId);
            if (nodePtr) {
                outModel.insert(std::move(nodePtr));
            }
        }
    }

    // 5. Rehydrate Graph Edges
    outGraph.clear();
    SqliteStatement edgeStmt(m_db, "SELECT edge_id, source_node_id, target_node_id, direction, "
                                   "color, stroke_width, arrow_style, label FROM graph_edges;");
    if (edgeStmt.isValid()) {
        while (edgeStmt.step()) {
            GraphEdge edge;
            edge.id = reinterpret_cast<const char*>(sqlite3_column_text(edgeStmt.get(), 0));
            edge.sourceNodeId =
                reinterpret_cast<const char*>(sqlite3_column_text(edgeStmt.get(), 1));
            edge.targetNodeId =
                reinterpret_cast<const char*>(sqlite3_column_text(edgeStmt.get(), 2));
            edge.direction = static_cast<EdgeDirection>(sqlite3_column_int(edgeStmt.get(), 3));
            uint32_t colorU32 = static_cast<uint32_t>(sqlite3_column_int64(edgeStmt.get(), 4));
            edge.color.r = static_cast<unsigned char>((colorU32 >> 24) & 0xFF);
            edge.color.g = static_cast<unsigned char>((colorU32 >> 16) & 0xFF);
            edge.color.b = static_cast<unsigned char>((colorU32 >> 8) & 0xFF);
            edge.color.a = static_cast<unsigned char>(colorU32 & 0xFF);
            edge.strokeWidth = sqlite3_column_double(edgeStmt.get(), 5);
            edge.arrowStyle = static_cast<ArrowStyle>(sqlite3_column_int(edgeStmt.get(), 6));
            const char* lbl = reinterpret_cast<const char*>(sqlite3_column_text(edgeStmt.get(), 7));
            if (lbl)
                edge.label = lbl;

            outGraph.addEdge(edge);
        }
    }
    return true;
}

std::vector<SearchResult> ProjectStore::executeSearch(const std::string& query) const {
    std::vector<SearchResult> results;
    if (!m_db || query.empty())
        return results;

    SqliteStatement stmt(m_db, "SELECT entity_id, entity_type, page_index, text_content FROM "
                               "fts_universal_index WHERE fts_universal_index MATCH ?;");
    if (!stmt.isValid())
        return results;

    sqlite3_bind_text(stmt.get(), 1, query.c_str(), -1, SQLITE_STATIC);
    while (stmt.step()) {
        SearchResult sr;
        sr.entityId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        sr.entityType = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        if (sqlite3_column_type(stmt.get(), 2) != SQLITE_NULL) {
            sr.pageIndex = sqlite3_column_int(stmt.get(), 2);
        } else {
            sr.pageIndex = -1;
        }
        const char* snippet = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
        if (snippet)
            sr.snippet = snippet;
        results.push_back(std::move(sr));
    }
    return results;
}

} // namespace FluidCore
