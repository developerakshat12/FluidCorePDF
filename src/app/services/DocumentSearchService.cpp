#include "DocumentSearchService.h"
#include "MemoryTelemetry.h"
#include "services/PdfDocumentService.h"

#include <algorithm>
#include <gtk/gtk.h>

#ifdef FLUIDCORE_HAS_MIMALLOC
#include <mimalloc.h>
#endif

#ifdef _WIN32
#include <malloc.h>
#endif

namespace FluidCoreApp {

DocumentSearchService::DocumentSearchService() : m_alive(std::make_shared<bool>(true)) {
    ensureWorkerStarted();
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

    const bool telemetryEnabled = (g_getenv("FLUIDCORE_SEARCH_BENCHMARK") != nullptr) ||
                                  (g_getenv("FLUIDCORE_LOG_TELEMETRY") != nullptr);

    MemoryTelemetry::ProcessHeapMetrics startMetrics{};
    if (telemetryEnabled) {
        startMetrics = MemoryTelemetry::getHeapMetrics();
        MemoryTelemetry::log(
            "[Search] === START SEARCH (EPHEMERAL HANDLES) === Query: \"" + query +
            "\" | Pages to scan: " + std::to_string(pages.size()) +
            " | Priv: " + MemoryTelemetry::formatMB(startMetrics.privateBytes) +
            " | LiveAlloc: " + MemoryTelemetry::formatMB(startMetrics.heapAllocated) +
            " | HeapCommit: " + MemoryTelemetry::formatMB(startMetrics.heapCommitted) +
            " | Slack: " + MemoryTelemetry::formatMB(startMetrics.heapSlack) +
            " | WS: " + MemoryTelemetry::formatMB(startMetrics.workingSet));
    }

    const PopplerFindFlags flags =
        caseSensitive ? POPPLER_FIND_CASE_SENSITIVE : POPPLER_FIND_DEFAULT;

    const int docPageCount = [document]() {
        std::lock_guard<std::mutex> popplerLock(PdfDocumentService::globalPopplerMutex());
        return poppler_document_get_n_pages(document);
    }();

    const bool benchmarkMode = (g_getenv("FLUIDCORE_SEARCH_BENCHMARK") != nullptr);

    long long cumDeltaGetPage = 0;
    long long cumDeltaFindText = 0;
    long long cumDeltaExtractFreeMatches = 0;
    long long cumDeltaUnrefPage = 0;

    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (m_cancelRequested || (m_activeSearchId != 0 && m_activeSearchId != m_currentSearchId)) {
            MemoryTelemetry::log("[Search] Search cancelled at page " + std::to_string(i));
            return {};
        }

        if (static_cast<int>(i) >= docPageCount) {
            break;
        }

        const std::size_t s0 = benchmarkMode ? MemoryTelemetry::getProcessPrivateBytes() : 0;

        PopplerPage* ephemeralPage = nullptr;
        {
            std::lock_guard<std::mutex> popplerLock(PdfDocumentService::globalPopplerMutex());
            ephemeralPage = poppler_document_get_page(document, static_cast<int>(i));
            if (ephemeralPage) {
                PopplerLifetimeTracker::onPageCreated(
                    ephemeralPage, i, "DocumentSearchService::searchSync (ephemeral)");
            }
        }

        const std::size_t s1 = benchmarkMode ? MemoryTelemetry::getProcessPrivateBytes() : 0;

        GList* matches = nullptr;
        if (ephemeralPage) {
            std::lock_guard<std::mutex> popplerLock(PdfDocumentService::globalPopplerMutex());
            matches = poppler_page_find_text_with_options(ephemeralPage, query.c_str(), flags);
        }

        const std::size_t s2 = benchmarkMode ? MemoryTelemetry::getProcessPrivateBytes() : 0;

        if (ephemeralPage) {
            const double pHeight = pages[i].height;
            for (GList* l = matches; l != nullptr; l = l->next) {
                auto* rect = static_cast<PopplerRectangle*>(l->data);
                if (rect) {
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

        const std::size_t s3 = benchmarkMode ? MemoryTelemetry::getProcessPrivateBytes() : 0;

        if (ephemeralPage) {
            std::lock_guard<std::mutex> popplerLock(PdfDocumentService::globalPopplerMutex());
            g_object_unref(ephemeralPage);
            PopplerLifetimeTracker::onPageDestroyed(
                ephemeralPage, "DocumentSearchService::searchSync (ephemeral)");
        }

        const std::size_t s4 = benchmarkMode ? MemoryTelemetry::getProcessPrivateBytes() : 0;

        if (benchmarkMode) {
            cumDeltaGetPage += (static_cast<long long>(s1) - static_cast<long long>(s0));
            cumDeltaFindText += (static_cast<long long>(s2) - static_cast<long long>(s1));
            cumDeltaExtractFreeMatches += (static_cast<long long>(s3) - static_cast<long long>(s2));
            cumDeltaUnrefPage += (static_cast<long long>(s4) - static_cast<long long>(s3));

            if (s4 > s0 && (s4 - s0) >= 2 * 1024 * 1024) {
                MemoryTelemetry::log("[Search Spike] Page " + std::to_string(i) + " spiked by +" +
                                     MemoryTelemetry::formatMB(s4 - s0) +
                                     " (Current: " + MemoryTelemetry::formatMB(s4) + ")");
            }

            if ((i + 1) % 100 == 0 || (i + 1) == pages.size()) {
                const std::size_t currPriv = s4;
                long long deltaFromStart = static_cast<long long>(currPriv) -
                                           static_cast<long long>(startMetrics.privateBytes);
                std::string sign = deltaFromStart >= 0 ? "+" : "-";
                std::size_t absDelta = deltaFromStart >= 0 ? deltaFromStart : -deltaFromStart;
                MemoryTelemetry::log(
                    "[Search Progress] Pages 0-" + std::to_string(i) + "/" +
                    std::to_string(pages.size()) + ": " + MemoryTelemetry::formatMB(currPriv) +
                    " (" + sign + MemoryTelemetry::formatMB(absDelta) +
                    " from start) | Hits so far: " + std::to_string(results.size()));
            }
        }
    }

    if (telemetryEnabled) {
        const auto endMetrics = MemoryTelemetry::getHeapMetrics();
        const long long deltaPriv = static_cast<long long>(endMetrics.privateBytes) -
                                    static_cast<long long>(startMetrics.privateBytes);
        const long long deltaLive = static_cast<long long>(endMetrics.heapAllocated) -
                                    static_cast<long long>(startMetrics.heapAllocated);
        const long long deltaCommit = static_cast<long long>(endMetrics.heapCommitted) -
                                      static_cast<long long>(startMetrics.heapCommitted);
        const long long deltaSlack = static_cast<long long>(endMetrics.heapSlack) -
                                     static_cast<long long>(startMetrics.heapSlack);

        if (benchmarkMode) {
            MemoryTelemetry::log(
                "[Search Internal Breakdown across " + std::to_string(pages.size()) + " pages]\n" +
                "    1. poppler_document_get_page:      " +
                MemoryTelemetry::formatSignedMB(cumDeltaGetPage) + "\n" +
                "    2. poppler_page_find_text:        " +
                MemoryTelemetry::formatSignedMB(cumDeltaFindText) + "\n" +
                "    3. match extraction & g_list_free: " +
                MemoryTelemetry::formatSignedMB(cumDeltaExtractFreeMatches) + "\n" +
                "    4. g_object_unref(ephemeralPage):  " +
                MemoryTelemetry::formatSignedMB(cumDeltaUnrefPage) + "\n" +
                "    Net per-page stage sum:           " +
                MemoryTelemetry::formatSignedMB(cumDeltaGetPage + cumDeltaFindText +
                                                cumDeltaExtractFreeMatches + cumDeltaUnrefPage) +
                "\n" + "    Total Search Net Delta:           " +
                MemoryTelemetry::formatSignedMB(deltaPriv));
        }

        MemoryTelemetry::log(
            "[Search] === COMPLETED SEARCH === Query: \"" + query +
            "\" | Total Hits: " + std::to_string(results.size()) + "\n" +
            "    Priv:       " + MemoryTelemetry::formatMB(startMetrics.privateBytes) + " -> " +
            MemoryTelemetry::formatMB(endMetrics.privateBytes) + " (" +
            MemoryTelemetry::formatSignedMB(deltaPriv) + ")\n" +
            "    LiveAlloc:  " + MemoryTelemetry::formatMB(startMetrics.heapAllocated) + " -> " +
            MemoryTelemetry::formatMB(endMetrics.heapAllocated) + " (" +
            MemoryTelemetry::formatSignedMB(deltaLive) + ") [Active C++ objects]\n" +
            "    HeapCommit: " + MemoryTelemetry::formatMB(startMetrics.heapCommitted) + " -> " +
            MemoryTelemetry::formatMB(endMetrics.heapCommitted) + " (" +
            MemoryTelemetry::formatSignedMB(deltaCommit) + ")\n" +
            "    Slack:      " + MemoryTelemetry::formatMB(startMetrics.heapSlack) + " -> " +
            MemoryTelemetry::formatMB(endMetrics.heapSlack) + " (" +
            MemoryTelemetry::formatSignedMB(deltaSlack) + ") [LFH retention]\n" +
            "    WS:         " + MemoryTelemetry::formatMB(startMetrics.workingSet) + " -> " +
            MemoryTelemetry::formatMB(endMetrics.workingSet));

        // Regression Guard: ephemeral page search must not cause unbounded Poppler page cache
        // retention.
        if (deltaPriv > 100 * 1024 * 1024) {
            MemoryTelemetry::log("[REGRESSION WARNING] Search memory growth (" +
                                 MemoryTelemetry::formatSignedMB(deltaPriv) +
                                 ") exceeded the 100 MB ephemeral threshold!");
        } else {
            MemoryTelemetry::log("[Search Regression Guard PASS] Search memory delta (" +
                                 MemoryTelemetry::formatSignedMB(deltaPriv) +
                                 ") is well within the 100 MB limit.");
        }
    }

    // Guarantee document-order ascending sort
    std::sort(results.begin(), results.end());
    return results;
}

void DocumentSearchService::searchAsync(PopplerDocument* document, const std::string& pdfPath,
                                        const std::vector<SearchPageLayout>& pages,
                                        const std::string& query,
                                        std::function<void(std::vector<SearchHit>)> onComplete,
                                        bool caseSensitive) {

    if (query.empty() || (!document && pdfPath.empty()) || pages.empty()) {
        cancel();
        if (onComplete) {
            onComplete({});
        }
        return;
    }

    ensureWorkerStarted();

    m_cancelRequested = false;
    const uint64_t searchId = ++m_currentSearchId;

    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_pendingRequest = SearchRequest{
            searchId, document, pdfPath, pages, query, std::move(onComplete), caseSensitive};
    }
    m_searchCv.notify_one();
}

void DocumentSearchService::ensureWorkerStarted() {
    std::lock_guard<std::mutex> lock(m_searchMutex);
    if (m_workerThread.joinable() && !m_workerRunning.load()) {
        m_workerThread.join();
    }
    if (!m_workerThread.joinable() && !m_exitRequested.load()) {
        m_workerRunning = true;
        m_workerThread = std::thread(&DocumentSearchService::workerLoop, this);
    }
}

void DocumentSearchService::workerLoop() {
    m_workerRunning = true;
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

        const char* ephemEnv = g_getenv("FLUIDCORE_EPHEMERAL_SEARCH");
        const bool useEphemeral = (ephemEnv == nullptr || std::string(ephemEnv) != "0");
        PopplerDocument* targetDoc = req.document;
        PopplerDocument* ephemeralDoc = nullptr;

        if (useEphemeral && !req.pdfPath.empty()) {
            GFile* file = g_file_new_for_path(req.pdfPath.c_str());
            char* uri = g_file_get_uri(file);
            GError* err = nullptr;
            {
                std::lock_guard<std::mutex> lock(PdfDocumentService::globalPopplerMutex());
                ephemeralDoc = poppler_document_new_from_file(uri, nullptr, &err);
            }
            g_free(uri);
            g_object_unref(file);

            if (ephemeralDoc) {
                targetDoc = ephemeralDoc;
                MemoryTelemetry::log(
                    "[Search Mode: EPHEMERAL DOCUMENT] (FLUIDCORE_EPHEMERAL_SEARCH=" +
                    std::string(ephemEnv ? ephemEnv : "default") +
                    ") Opened throwaway PopplerDocument on worker thread.");
            } else {
                MemoryTelemetry::log("[Search Mode: EPHEMERAL DOCUMENT] Fallback to persistent "
                                     "doc: could not open ephemeral doc (" +
                                     (err ? std::string(err->message) : "unknown error") + ")");
                if (err)
                    g_error_free(err);
            }
        } else {
            MemoryTelemetry::log(
                "[Search Mode: PERSISTENT DOCUMENT] Running search against GUI m_document.");
        }

        auto hits = searchSync(targetDoc, req.pages, req.query, req.caseSensitive);

        if (ephemeralDoc) {
            const bool telemetryAsync = (g_getenv("FLUIDCORE_SEARCH_BENCHMARK") != nullptr) ||
                                        (g_getenv("FLUIDCORE_LOG_TELEMETRY") != nullptr);
            MemoryTelemetry::ProcessHeapMetrics mBefore{};
            if (telemetryAsync) {
                mBefore = MemoryTelemetry::getHeapMetrics();
            }
            {
                std::lock_guard<std::mutex> lock(PdfDocumentService::globalPopplerMutex());
                g_object_unref(ephemeralDoc);
            }
#ifdef FLUIDCORE_HAS_MIMALLOC
            mi_collect(true);
#endif
#ifdef _WIN32
            _heapmin();
#endif
            if (telemetryAsync) {
                const auto mAfter = MemoryTelemetry::getHeapMetrics();
                const long long deltaLive = static_cast<long long>(mAfter.heapAllocated) -
                                            static_cast<long long>(mBefore.heapAllocated);
                const long long deltaCommit = static_cast<long long>(mAfter.heapCommitted) -
                                              static_cast<long long>(mBefore.heapCommitted);
                const long long deltaSlack = static_cast<long long>(mAfter.heapSlack) -
                                             static_cast<long long>(mBefore.heapSlack);
                const long long deltaPriv = static_cast<long long>(mAfter.privateBytes) -
                                            static_cast<long long>(mBefore.privateBytes);

                MemoryTelemetry::log("[Search Mode: EPHEMERAL DOCUMENT] Destroyed throwaway "
                                     "PopplerDocument & Purged Heap:\n"
                                     "    LiveAlloc:  " +
                                     MemoryTelemetry::formatMB(mBefore.heapAllocated) + " -> " +
                                     MemoryTelemetry::formatMB(mAfter.heapAllocated) + " (" +
                                     MemoryTelemetry::formatSignedMB(deltaLive) +
                                     ") [Document C++ objects freed]\n"
                                     "    HeapCommit: " +
                                     MemoryTelemetry::formatMB(mBefore.heapCommitted) + " -> " +
                                     MemoryTelemetry::formatMB(mAfter.heapCommitted) + " (" +
                                     MemoryTelemetry::formatSignedMB(deltaCommit) +
                                     ")\n"
                                     "    Slack:      " +
                                     MemoryTelemetry::formatMB(mBefore.heapSlack) + " -> " +
                                     MemoryTelemetry::formatMB(mAfter.heapSlack) + " (" +
                                     MemoryTelemetry::formatSignedMB(deltaSlack) +
                                     ") [LFH retention]\n"
                                     "    Priv:       " +
                                     MemoryTelemetry::formatMB(mBefore.privateBytes) + " -> " +
                                     MemoryTelemetry::formatMB(mAfter.privateBytes) + " (" +
                                     MemoryTelemetry::formatSignedMB(deltaPriv) + ")");
            }
        }

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

        // Periodic worker thread recycle when idle to release thread-scoped OS resources
        m_searchesProcessed++;
        if (m_searchesProcessed.load() >= 8) {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            if (!m_pendingRequest.has_value()) {
                m_searchesProcessed = 0;
                m_workerRunning = false;
                break;
            }
        }
    }
    m_workerRunning = false;
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
