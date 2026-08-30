#include "DocumentSearchService.h"

#include <algorithm>
#include <gtk/gtk.h>

namespace FluidCoreApp {

DocumentSearchService::DocumentSearchService() : m_alive(std::make_shared<bool>(true)) {
    m_workerThread = std::thread(&DocumentSearchService::workerLoop, this);
}

DocumentSearchService::~DocumentSearchService() {
    *m_alive = false;
    m_exitRequested = true;
    m_cancelRequested = true;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_pendingRequest.reset();
    }
    m_searchCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void DocumentSearchService::cancel() {
    m_cancelRequested = true;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_pendingRequest.reset();
    }
}

std::vector<SearchHit> DocumentSearchService::searchSync(PopplerDocument* document,
                                                         const std::vector<SearchPageLayout>& pages,
                                                         const std::string& query,
                                                         bool caseSensitive) {
    std::vector<SearchHit> results;
    if (!document || pages.empty() || query.empty()) {
        return results;
    }

    const PopplerFindFlags flags =
        caseSensitive ? POPPLER_FIND_CASE_SENSITIVE : POPPLER_FIND_DEFAULT;

    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (m_cancelRequested || (m_activeSearchId != 0 && m_activeSearchId != m_currentSearchId)) {
            return {};
        }

        PopplerPage* page = pages[i].page;
        if (!page) {
            continue;
        }

        GList* matches = poppler_page_find_text_with_options(page, query.c_str(), flags);
        const double pHeight = pages[i].height;
        for (GList* l = matches; l != nullptr; l = l->next) {
            auto* rect = static_cast<PopplerRectangle*>(l->data);
            if (rect) {
                // Convert Poppler bottom-left PDF coordinate system to Cairo top-left coordinate
                // system
                const double cairoY0 = pHeight - std::max(rect->y1, rect->y2);
                const double cairoY1 = pHeight - std::min(rect->y1, rect->y2);
                const double rMinX = std::min(rect->x1, rect->x2);
                const double rMaxX = std::max(rect->x1, rect->x2);

                const double docY0 = pages[i].y + cairoY0;
                const double docY1 = pages[i].y + cairoY1;

                SearchHit hit;
                hit.pageIndex = i;
                hit.pageBounds = PopplerRectangle{rMinX, cairoY0, rMaxX, cairoY1};
                hit.docYStart = docY0;
                hit.docYEnd = docY1;
                results.push_back(hit);

                poppler_rectangle_free(rect);
            }
        }
        if (matches) {
            g_list_free(matches);
        }
    }

    // Guarantee document-order ascending sort
    std::sort(results.begin(), results.end());
    return results;
}

void DocumentSearchService::searchAsync(PopplerDocument* document,
                                        const std::vector<SearchPageLayout>& pages,
                                        const std::string& query,
                                        std::function<void(std::vector<SearchHit>)> onComplete,
                                        bool caseSensitive) {

    if (query.empty() || !document || pages.empty()) {
        cancel();
        if (onComplete) {
            onComplete({});
        }
        return;
    }

    m_cancelRequested = false;
    const uint64_t searchId = ++m_currentSearchId;

    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_pendingRequest =
            SearchRequest{searchId, document, pages, query, std::move(onComplete), caseSensitive};
    }
    m_searchCv.notify_one();
}

void DocumentSearchService::workerLoop() {
    while (!m_exitRequested) {
        SearchRequest req;
        {
            std::unique_lock<std::mutex> lock(m_searchMutex);
            m_searchCv.wait(
                lock, [this]() { return m_exitRequested.load() || m_pendingRequest.has_value(); });

            if (m_exitRequested) {
                break;
            }

            req = std::move(*m_pendingRequest);
            m_pendingRequest.reset();
            m_activeSearchId = req.searchId;
        }

        if (m_cancelRequested || req.searchId != m_currentSearchId) {
            m_activeSearchId = 0;
            continue;
        }

        auto hits = searchSync(req.document, req.pages, req.query, req.caseSensitive);

        if (!m_cancelRequested && req.searchId == m_currentSearchId && req.onComplete &&
            !m_exitRequested) {
            struct CallbackData {
                std::function<void(std::vector<SearchHit>)> callback;
                std::vector<SearchHit> results;
                std::shared_ptr<bool> alive;
            };

            auto* cbData = new CallbackData{std::move(req.onComplete), std::move(hits), m_alive};
            g_idle_add(
                +[](gpointer data) -> gboolean {
                    auto* d = static_cast<CallbackData*>(data);
                    if (*d->alive && d->callback) {
                        d->callback(std::move(d->results));
                    }
                    delete d;
                    return G_SOURCE_REMOVE;
                },
                cbData);
        }

        m_activeSearchId = 0;
    }
}

std::vector<FluidCore::SearchHitSpan>
DocumentSearchService::toHitSpans(const std::vector<SearchHit>& hits) {
    std::vector<FluidCore::SearchHitSpan> spans;
    spans.reserve(hits.size());
    for (const auto& h : hits) {
        spans.push_back(FluidCore::SearchHitSpan{h.docYStart, h.docYEnd});
    }
    return spans;
}

} // namespace FluidCoreApp
