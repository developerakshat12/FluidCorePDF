#include "PdfDocumentService.h"
#include "services/PdfExportService.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace FluidCoreApp {

PdfDocumentService::~PdfDocumentService() {
    clear();
}

namespace {
std::string normalizeForMatch(const std::string& s) {
    std::filesystem::path p(s);
    std::string fn = p.filename().string();
    std::transform(fn.begin(), fn.end(), fn.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return fn;
}
} // namespace

PdfDocumentService::DocEntry* PdfDocumentService::resolveEntryLocked(const std::string& docId) {
    if (m_documents.empty()) {
        return nullptr;
    }
    if (docId.empty()) {
        return &m_documents.begin()->second;
    }
    // 1. Direct match
    auto it = m_documents.find(docId);
    if (it != m_documents.end()) {
        return &it->second;
    }

    // 2. Match by filePath
    for (auto& [id, entry] : m_documents) {
        if (entry.filePath == docId) {
            return &entry;
        }
    }

    // 3. Match by normalized filename
    std::string targetNorm = normalizeForMatch(docId);
    if (!targetNorm.empty()) {
        for (auto& [id, entry] : m_documents) {
            if (normalizeForMatch(entry.filePath) == targetNorm ||
                normalizeForMatch(entry.docId) == targetNorm ||
                normalizeForMatch(id) == targetNorm) {
                return &entry;
            }
        }
    }

    // 4. Auto-register if docId is an existing file on disk
    std::filesystem::path targetP(docId);
    std::error_code ec;
    if (std::filesystem::exists(targetP, ec) && !std::filesystem::is_directory(targetP, ec)) {
        DocEntry newEntry;
        newEntry.docId = docId;
        newEntry.filePath = targetP.string();
        newEntry.mainDoc = nullptr;
        auto [insertedIt, success] = m_documents.emplace(docId, std::move(newEntry));
        return &insertedIt->second;
    }

    // 5. If only 1 document is open in project, fallback to it
    if (m_documents.size() == 1) {
        return &m_documents.begin()->second;
    }

    // 6. Fallback to entry with active mainDoc if available
    for (auto& [id, entry] : m_documents) {
        if (entry.mainDoc) {
            return &entry;
        }
    }

    return nullptr;
}

const PdfDocumentService::DocEntry*
PdfDocumentService::resolveEntryLocked(const std::string& docId) const {
    if (m_documents.empty()) {
        return nullptr;
    }
    if (docId.empty()) {
        return &m_documents.begin()->second;
    }
    auto it = m_documents.find(docId);
    if (it != m_documents.end()) {
        return &it->second;
    }
    for (const auto& [id, entry] : m_documents) {
        if (entry.filePath == docId) {
            return &entry;
        }
    }
    std::string targetNorm = normalizeForMatch(docId);
    if (!targetNorm.empty()) {
        for (const auto& [id, entry] : m_documents) {
            if (normalizeForMatch(entry.filePath) == targetNorm ||
                normalizeForMatch(entry.docId) == targetNorm ||
                normalizeForMatch(id) == targetNorm) {
                return &entry;
            }
        }
    }
    if (m_documents.size() == 1) {
        return &m_documents.begin()->second;
    }
    for (const auto& [id, entry] : m_documents) {
        if (entry.mainDoc) {
            return &entry;
        }
    }
    return nullptr;
}

void PdfDocumentService::registerMainDocument(const std::string& docId, PopplerDocument* doc,
                                              const std::string& filePath) {
    std::lock_guard<std::mutex> popplerLock(globalPopplerMutex());
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_cancelledDocIds.erase(docId);

    auto& entry = m_documents[docId];
    entry.docId = docId;
    entry.filePath = filePath;
    entry.mainDoc = doc;
    entry.backgroundDoc.reset(); // Re-create on demand
}

void PdfDocumentService::unregisterDocument(const std::string& docId) {
    std::lock_guard<std::mutex> popplerLock(globalPopplerMutex());
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_cancelledDocIds.insert(docId);
    m_documents.erase(docId);
}

PopplerDocument* PdfDocumentService::getMainDocument(const std::string& docId) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    const auto* entry = resolveEntryLocked(docId);
    if (entry) {
        return entry->mainDoc;
    }
    return nullptr;
}

std::string PdfDocumentService::getFilePath(const std::string& docId) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    const auto* entry = resolveEntryLocked(docId);
    if (entry) {
        return entry->filePath;
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> PdfDocumentService::allDocuments() const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(m_documents.size());
    for (const auto& [docId, entry] : m_documents) {
        result.emplace_back(docId, entry.filePath);
    }
    return result;
}

bool PdfDocumentService::repointDocumentPath(const std::string& docId, const std::string& newPath) {
    std::lock_guard<std::mutex> popplerLock(globalPopplerMutex());
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto* entry = resolveEntryLocked(docId);
    if (entry) {
        entry->filePath = newPath;
        entry->backgroundDoc.reset(); // Re-open background instance on next demand
        if (m_documents.find(newPath) == m_documents.end()) {
            DocEntry alias;
            alias.docId = newPath;
            alias.filePath = newPath;
            alias.mainDoc = entry->mainDoc;
            m_documents.emplace(newPath, std::move(alias));
        }
        return true;
    }
    return false;
}

PopplerPagePtr PdfDocumentService::getMainPage(const std::string& docId, std::size_t pageNo) const {
    PopplerDocument* doc = getMainDocument(docId);
    if (!doc) {
        return nullptr;
    }

    int numPages = poppler_document_get_n_pages(doc);
    if (static_cast<int>(pageNo) >= numPages) {
        return nullptr;
    }

    PopplerPage* page = poppler_document_get_page(doc, static_cast<int>(pageNo));
    return PopplerPagePtr(page);
}

PopplerPagePtr PdfDocumentService::getBackgroundPage(const std::string& docId, std::size_t pageNo) {
    std::lock_guard<std::mutex> workerLock(globalPopplerMutex());

    std::string filePath;
    {
        std::lock_guard<std::mutex> regLock(m_registryMutex);
        if (m_cancelledDocIds.count(docId)) {
            return nullptr;
        }
        auto* entry = resolveEntryLocked(docId);
        if (!entry) {
            return nullptr;
        }
        filePath = entry->filePath;
    }

    if (filePath.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> regLock(m_registryMutex);
    auto* entry = resolveEntryLocked(docId);
    if (!entry) {
        return nullptr;
    }

    PopplerDocument* docToUse = entry->backgroundDoc ? entry->backgroundDoc.get() : entry->mainDoc;
    if (!docToUse) {
        for (const auto& [id, otherEntry] : m_documents) {
            if (otherEntry.mainDoc) {
                docToUse = otherEntry.mainDoc;
                break;
            }
        }
    }
    if (!docToUse) {
        GError* error = nullptr;
        char* uri = nullptr;
        if (filePath.rfind("file://", 0) == 0) {
            uri = g_strdup(filePath.c_str());
        } else {
            std::filesystem::path fp(filePath);
            std::string normPath = fp.lexically_normal().string();
            uri = g_filename_to_uri(normPath.c_str(), nullptr, &error);
        }

        if (uri) {
            PopplerDocument* bgDoc = poppler_document_new_from_file(uri, nullptr, &error);
            g_free(uri);
            if (bgDoc) {
                entry->backgroundDoc.reset(bgDoc);
                docToUse = bgDoc;
            } else if (error) {
                std::cerr << "[PdfDocumentService] Failed to load bgDoc: " << error->message
                          << "\n";
                g_error_free(error);
            }
        } else if (error) {
            std::cerr << "[PdfDocumentService] Failed to get URI for '" << filePath
                      << "': " << error->message << "\n";
            g_error_free(error);
        }
    }

    if (!docToUse) {
        return nullptr;
    }

    int numPages = poppler_document_get_n_pages(docToUse);
    if (static_cast<int>(pageNo) >= numPages) {
        return nullptr;
    }

    PopplerPage* page = poppler_document_get_page(docToUse, static_cast<int>(pageNo));
    return PopplerPagePtr(page);
}

CairoSurfaceHandle PdfDocumentService::renderBackgroundCrop(
    const std::string& docId, std::size_t pageNo, const FluidCore::Rectangle& normRect, int targetW,
    int targetH, const std::vector<FluidCore::Stroke>& strokes) {
    std::lock_guard<std::mutex> workerLock(globalPopplerMutex());

    std::string filePath;
    {
        std::lock_guard<std::mutex> regLock(m_registryMutex);
        if (m_cancelledDocIds.count(docId)) {
            return CairoSurfaceHandle{};
        }
        auto* entry = resolveEntryLocked(docId);
        if (!entry) {
            return CairoSurfaceHandle{};
        }
        filePath = entry->filePath;
    }

    if (filePath.empty()) {
        return CairoSurfaceHandle{};
    }

    std::lock_guard<std::mutex> regLock(m_registryMutex);
    auto* entry = resolveEntryLocked(docId);
    if (!entry) {
        return CairoSurfaceHandle{};
    }

    PopplerDocument* docToUse = entry->backgroundDoc ? entry->backgroundDoc.get() : entry->mainDoc;
    if (!docToUse) {
        for (const auto& [id, otherEntry] : m_documents) {
            if (otherEntry.mainDoc) {
                docToUse = otherEntry.mainDoc;
                break;
            }
        }
    }
    if (!docToUse) {
        GError* error = nullptr;
        char* uri = nullptr;
        if (filePath.rfind("file://", 0) == 0) {
            uri = g_strdup(filePath.c_str());
        } else {
            std::filesystem::path fp(filePath);
            std::string normPath = fp.lexically_normal().string();
            uri = g_filename_to_uri(normPath.c_str(), nullptr, &error);
        }

        if (uri) {
            PopplerDocument* bgDoc = poppler_document_new_from_file(uri, nullptr, &error);
            g_free(uri);
            if (bgDoc) {
                entry->backgroundDoc.reset(bgDoc);
                docToUse = bgDoc;
            } else if (error) {
                std::cerr << "[PdfDocumentService] Failed to load bgDoc: " << error->message
                          << "\n";
                g_error_free(error);
            }
        } else if (error) {
            std::cerr << "[PdfDocumentService] Failed to get URI for '" << filePath
                      << "': " << error->message << "\n";
            g_error_free(error);
        }
    }

    if (!docToUse) {
        return CairoSurfaceHandle{};
    }

    int numPages = poppler_document_get_n_pages(docToUse);
    if (static_cast<int>(pageNo) >= numPages) {
        return CairoSurfaceHandle{};
    }

    PopplerPage* page = poppler_document_get_page(docToUse, static_cast<int>(pageNo));
    if (!page) {
        return CairoSurfaceHandle{};
    }

    double origWidth = 0.0, origHeight = 0.0;
    poppler_page_get_size(page, &origWidth, &origHeight);
    if (origWidth <= 0.0 || origHeight <= 0.0) {
        g_object_unref(page);
        return CairoSurfaceHandle{};
    }

    double cropX = std::clamp(normRect.x, 0.0, 1.0) * origWidth;
    double cropY = std::clamp(normRect.y, 0.0, 1.0) * origHeight;
    double cropW = std::clamp(normRect.w, 0.001, 1.0) * origWidth;
    double cropH = std::clamp(normRect.h, 0.001, 1.0) * origHeight;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, targetW, targetH);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        g_object_unref(page);
        return CairoSurfaceHandle{};
    }

    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_scale(cr, static_cast<double>(targetW) / cropW, static_cast<double>(targetH) / cropH);
    cairo_translate(cr, -cropX, -cropY);

    poppler_page_render(page, cr);
    for (const auto& stroke : strokes) {
        PdfExportService::renderStroke(cr, stroke);
    }
    cairo_destroy(cr);

    g_object_unref(page);

    return CairoSurfaceHandle(surface, true);
}

bool PdfDocumentService::isDocumentCancelled(const std::string& docId) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    return m_cancelledDocIds.count(docId) > 0;
}

void PdfDocumentService::cancelDocumentRequests(const std::string& docId) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_cancelledDocIds.insert(docId);
}

void PdfDocumentService::clear() {
    std::lock_guard<std::mutex> workerLock(globalPopplerMutex());
    std::lock_guard<std::mutex> regLock(m_registryMutex);
    m_documents.clear();
    m_cancelledDocIds.clear();
}

} // namespace FluidCoreApp
