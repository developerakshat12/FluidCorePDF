#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glib-object.h>
#include <poppler.h>

#include "FluidCoreAPI.h"
#include "services/PageTileCache.h"
#include "storage/AnnotationStore.h"

namespace FluidCoreApp {

struct GObjectDeleter {
    void operator()(gpointer obj) const {
        if (obj) {
            g_object_unref(obj);
        }
    }
};

using PopplerDocumentPtr = std::unique_ptr<PopplerDocument, GObjectDeleter>;
using PopplerPagePtr = std::unique_ptr<PopplerPage, GObjectDeleter>;

// Multi-document resolution and lifecycle management service for FluidCore.
// Manages dual-instance PopplerDocument handles:
// 1. Main UI-thread handles accessed by DocumentPane with zero lock overhead.
// 2. Isolated background handles used by background rasterization threads,
//    synchronized via m_workerPopplerMutex to prevent UI thread lock contention.
class PdfDocumentService {
  public:
    PdfDocumentService() = default;
    ~PdfDocumentService();

    PdfDocumentService(const PdfDocumentService&) = delete;
    PdfDocumentService& operator=(const PdfDocumentService&) = delete;

    // Registers the actively open UI-thread PopplerDocument from DocumentPane
    void registerMainDocument(const std::string& docId, PopplerDocument* doc,
                              const std::string& filePath);

    // Unregisters document, closes background handles, and marks pending requests as cancelled
    void unregisterDocument(const std::string& docId);

    // UI-thread access (zero locking)
    PopplerDocument* getMainDocument(const std::string& docId) const;
    PopplerPagePtr getMainPage(const std::string& docId, std::size_t pageNo) const;
    std::string getFilePath(const std::string& docId) const;
    std::vector<std::pair<std::string, std::string>> allDocuments() const;
    bool repointDocumentPath(const std::string& docId, const std::string& newPath);

    // Background worker access (protected by m_workerPopplerMutex)
    PopplerPagePtr getBackgroundPage(const std::string& docId, std::size_t pageNo);
    CairoSurfaceHandle renderBackgroundCrop(const std::string& docId, std::size_t pageNo,
                                            const FluidCore::Rectangle& normRect, int targetW,
                                            int targetH,
                                            const std::vector<FluidCore::Stroke>& strokes = {});

    // In-flight cancellation query
    bool isDocumentCancelled(const std::string& docId) const;
    void cancelDocumentRequests(const std::string& docId);

    static std::mutex& globalPopplerMutex() {
        static std::mutex s_mutex;
        return s_mutex;
    }

    std::mutex& workerPopplerMutex() { return globalPopplerMutex(); }

    void clear();

  private:
    struct DocEntry {
        std::string docId;
        std::string filePath;
        PopplerDocument* mainDoc = nullptr; // Owned by DocumentPane or non-owning ref
        PopplerDocumentPtr backgroundDoc;   // Dedicated instance for background rendering
    };

    DocEntry* resolveEntryLocked(const std::string& docId);
    const DocEntry* resolveEntryLocked(const std::string& docId) const;

    mutable std::mutex m_registryMutex;
    std::mutex m_workerPopplerMutex;

    std::unordered_map<std::string, DocEntry> m_documents;
    std::unordered_set<std::string> m_cancelledDocIds;
};

} // namespace FluidCoreApp
