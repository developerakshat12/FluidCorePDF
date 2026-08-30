#include "PdfDocumentService.h"

#include <iostream>

namespace FluidCoreApp {

PdfDocumentService::~PdfDocumentService() {
    clear();
}

void PdfDocumentService::registerMainDocument(const std::string& docId, PopplerDocument* doc,
                                              const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_cancelledDocIds.erase(docId);

    auto& entry = m_documents[docId];
    entry.docId = docId;
    entry.filePath = filePath;
    entry.mainDoc = doc;
    entry.backgroundDoc.reset(); // Re-create on demand
}

void PdfDocumentService::unregisterDocument(const std::string& docId) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_cancelledDocIds.insert(docId);
    m_documents.erase(docId);
}

PopplerDocument* PdfDocumentService::getMainDocument(const std::string& docId) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto it = m_documents.find(docId);
    if (it != m_documents.end()) {
        return it->second.mainDoc;
    }
    return nullptr;
}

std::string PdfDocumentService::getFilePath(const std::string& docId) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto it = m_documents.find(docId);
    if (it != m_documents.end()) {
        return it->second.filePath;
    }
    return "";
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
    std::lock_guard<std::mutex> workerLock(m_workerPopplerMutex);

    std::string filePath;
    {
        std::lock_guard<std::mutex> regLock(m_registryMutex);
        if (m_cancelledDocIds.count(docId)) {
            return nullptr;
        }
        auto it = m_documents.find(docId);
        if (it == m_documents.end()) {
            return nullptr;
        }
        filePath = it->second.filePath;
    }

    if (filePath.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> regLock(m_registryMutex);
    auto it = m_documents.find(docId);
    if (it == m_documents.end()) {
        return nullptr;
    }

    if (!it->second.backgroundDoc) {
        GError* error = nullptr;
        char* uri = nullptr;
        if (filePath.rfind("file://", 0) == 0) {
            uri = g_strdup(filePath.c_str());
        } else {
            uri = g_filename_to_uri(filePath.c_str(), nullptr, &error);
        }

        if (uri) {
            PopplerDocument* bgDoc = poppler_document_new_from_file(uri, nullptr, &error);
            g_free(uri);
            if (bgDoc) {
                it->second.backgroundDoc.reset(bgDoc);
            } else if (error) {
                g_error_free(error);
            }
        } else if (error) {
            g_error_free(error);
        }
    }

    if (!it->second.backgroundDoc) {
        return nullptr;
    }

    int numPages = poppler_document_get_n_pages(it->second.backgroundDoc.get());
    if (static_cast<int>(pageNo) >= numPages) {
        return nullptr;
    }

    PopplerPage* page =
        poppler_document_get_page(it->second.backgroundDoc.get(), static_cast<int>(pageNo));
    return PopplerPagePtr(page);
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
    std::lock_guard<std::mutex> workerLock(m_workerPopplerMutex);
    std::lock_guard<std::mutex> regLock(m_registryMutex);
    m_documents.clear();
    m_cancelledDocIds.clear();
}

} // namespace FluidCoreApp
