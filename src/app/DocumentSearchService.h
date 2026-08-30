#pragma once

#include "search/SearchSqueezePlanner.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <poppler.h>

namespace FluidCoreApp {

struct SearchPageLayout {
    PopplerPage* page = nullptr;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct SearchHit {
    std::size_t pageIndex = 0;
    PopplerRectangle pageBounds{0.0, 0.0, 0.0, 0.0};
    double docYStart = 0.0;
    double docYEnd = 0.0;
    std::string snippet;

    bool operator<(const SearchHit& other) const {
        if (pageIndex != other.pageIndex) {
            return pageIndex < other.pageIndex;
        }
        return docYStart < other.docYStart;
    }
};

class DocumentSearchService {
  public:
    DocumentSearchService();
    ~DocumentSearchService();

    // Synchronous search across all document pages, returning strictly sorted hits.
    std::vector<SearchHit> searchSync(
        PopplerDocument* document,
        const std::vector<SearchPageLayout>& pages,
        const std::string& query,
        bool caseSensitive = false);

    // Asynchronous search running on a dedicated worker thread with cancellation support.
    // Dispatches results onto the GTK main loop via onComplete callback.
    void searchAsync(
        PopplerDocument* document,
        const std::vector<SearchPageLayout>& pages,
        const std::string& query,
        std::function<void(std::vector<SearchHit>)> onComplete,
        bool caseSensitive = false);

    // Cancels any in-flight asynchronous search.
    void cancel();

    // Extracts SearchHitSpan list for SearchSqueezePlanner.
    static std::vector<FluidCore::SearchHitSpan> toHitSpans(const std::vector<SearchHit>& hits);

  private:
    struct SearchRequest {
        uint64_t searchId = 0;
        PopplerDocument* document = nullptr;
        std::vector<SearchPageLayout> pages;
        std::string query;
        std::function<void(std::vector<SearchHit>)> onComplete;
        bool caseSensitive = false;
    };

    void workerLoop();

    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_exitRequested{false};
    std::atomic<uint64_t> m_currentSearchId{0};
    std::atomic<uint64_t> m_activeSearchId{0};

    std::shared_ptr<bool> m_alive;

    std::mutex m_searchMutex;
    std::condition_variable m_searchCv;
    std::optional<SearchRequest> m_pendingRequest;
    std::thread m_workerThread;
};

} // namespace FluidCoreApp
