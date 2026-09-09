#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#endif

#ifdef FLUIDCORE_HAS_MIMALLOC
#include <mimalloc.h>
#endif

namespace FluidCoreApp {

class MemoryTelemetry {
public:
    struct HeapMinResult {
        std::size_t privateBefore = 0;
        std::size_t privateAfter = 0;
        std::size_t wsBefore = 0;
        std::size_t wsAfter = 0;
        long long deltaBytes = 0;
        int status = 0;
    };

    static HeapMinResult runHeapMin(const std::string& checkpointTag) {
        HeapMinResult res;
#ifdef _WIN32
        res.privateBefore = getProcessPrivateBytes();
        res.wsBefore = getProcessWorkingSet();
        res.status = _heapmin();
        res.privateAfter = getProcessPrivateBytes();
        res.wsAfter = getProcessWorkingSet();
        res.deltaBytes = static_cast<long long>(res.privateAfter) - static_cast<long long>(res.privateBefore);

        std::string sign = res.deltaBytes >= 0 ? "+" : "-";
        std::size_t absD = res.deltaBytes >= 0 ? res.deltaBytes : -res.deltaBytes;
        log("[Heap Diagnostic: _heapmin()] === " + checkpointTag + " ===\n" +
            "    Private Bytes: " + formatMB(res.privateBefore) + " -> " + formatMB(res.privateAfter) +
            " (Delta: " + sign + formatMB(absD) + ")\n" +
            "    Working Set:   " + formatMB(res.wsBefore) + " -> " + formatMB(res.wsAfter) +
            " | UCRT _heapmin() return: " + std::to_string(res.status));
#endif
        return res;
    }
    struct ProcessHeapMetrics {
        std::size_t privateBytes = 0;
        std::size_t workingSet = 0;
        std::size_t heapAllocated = 0; // cbAllocated: live active C++ allocations
        std::size_t heapCommitted = 0; // cbCommitted: virtual pages committed by heap manager
        std::size_t heapSlack = 0;     // committed - allocated: LFH bucket retention / slack
        std::size_t mimallocCommitted = 0;
        std::size_t mimallocRss = 0;
    };

    static ProcessHeapMetrics getHeapMetrics() {
        ProcessHeapMetrics m;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            m.privateBytes = pmc.PrivateUsage;
            m.workingSet = pmc.WorkingSetSize;
        }

        DWORD numHeaps = GetProcessHeaps(0, nullptr);
        if (numHeaps > 0) {
            std::vector<HANDLE> heaps(numHeaps);
            DWORD actual = GetProcessHeaps(numHeaps, heaps.data());
            for (DWORD i = 0; i < actual; ++i) {
                HEAP_SUMMARY summary = {};
                summary.cb = sizeof(HEAP_SUMMARY);
                if (HeapSummary(heaps[i], 0, &summary)) {
                    m.heapAllocated += summary.cbAllocated;
                    m.heapCommitted += summary.cbCommitted;
                }
            }
        }
        if (m.heapCommitted >= m.heapAllocated) {
            m.heapSlack = m.heapCommitted - m.heapAllocated;
        }
#endif
#ifdef FLUIDCORE_HAS_MIMALLOC
        size_t elapsed = 0, user = 0, sys = 0, curRss = 0, peakRss = 0, curCommit = 0, peakCommit = 0, pageFaults = 0;
        mi_process_info(&elapsed, &user, &sys, &curRss, &peakRss, &curCommit, &peakCommit, &pageFaults);
        m.mimallocCommitted = curCommit;
        m.mimallocRss = curRss;
#endif
        return m;
    }

    static std::size_t getProcessPrivateBytes() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            return pmc.PrivateUsage;
        }
#endif
        return 0;
    }

    static std::size_t getProcessWorkingSet() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize;
        }
#endif
        return 0;
    }

    static std::string formatMB(std::size_t bytes) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
        return oss.str();
    }

    static std::string formatSignedMB(long long deltaBytes) {
        std::string sign = deltaBytes >= 0 ? "+" : "-";
        std::size_t absD = deltaBytes >= 0 ? deltaBytes : -deltaBytes;
        return sign + formatMB(absD);
    }

    static void log(const std::string& message) {
        static std::mutex s_logMutex;
        std::lock_guard<std::mutex> lock(s_logMutex);

        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        struct tm tmBuf;
#ifdef _WIN32
        localtime_s(&tmBuf, &timeT);
#else
        localtime_r(&timeT, &tmBuf);
#endif
        char timeStr[32];
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &tmBuf);

        std::string formatted = std::string("[") + timeStr + "] " + message;
        std::cout << formatted << std::endl;

        // Append to dedicated telemetry log file when FLUIDCORE_LOG_TELEMETRY or FLUIDCORE_TELEMETRY is enabled
        static const bool s_fileLogEnabled = []() {
            const char* envLog = std::getenv("FLUIDCORE_LOG_TELEMETRY");
            const char* envTel = std::getenv("FLUIDCORE_TELEMETRY");
            return (envLog && std::string(envLog) != "0") ||
                   (envTel && std::string(envTel) != "0");
        }();

        if (s_fileLogEnabled) {
            static std::ofstream s_fileLog("D:/FluidCorePDF/fluidcore_telemetry.log", std::ios::app);
            if (s_fileLog.is_open()) {
                s_fileLog << formatted << std::endl;
                s_fileLog.flush();
            }
        }
    }
};

class PopplerLifetimeTracker {
public:
    static void onPageCreated(void* /*pagePtr*/, std::size_t pageNo, const std::string& context) {
        std::lock_guard<std::mutex> lock(s_mutex);
        ++s_totalCreated;
        ++s_livePages;

        static const bool s_verbose = (std::getenv("FLUIDCORE_VERBOSE_TELEMETRY") != nullptr);
        if (s_verbose && (s_totalCreated % 200 == 0 || s_totalCreated == 1 || s_totalCreated == 892)) {
            MemoryTelemetry::log("[PopplerLifetime] Page created: page " + std::to_string(pageNo) +
                                 " (live: " + std::to_string(s_livePages.load()) +
                                 ", total created: " + std::to_string(s_totalCreated.load()) +
                                 ", ctx: " + context + ")");
        }
    }

    static void onPageDestroyed(void* /*pagePtr*/, const std::string& context) {
        std::lock_guard<std::mutex> lock(s_mutex);
        ++s_totalDestroyed;
        if (s_livePages > 0) {
            --s_livePages;
        }

        static const bool s_verbose = (std::getenv("FLUIDCORE_VERBOSE_TELEMETRY") != nullptr);
        if (!s_verbose) {
            return;
        }

        // For single-page interactive draws, log individually
        if (context.find("draw") != std::string::npos) {
            MemoryTelemetry::log("[PopplerLifetime] Page destroyed (live: " + std::to_string(s_livePages.load()) +
                                 ", total destroyed: " + std::to_string(s_totalDestroyed.load()) +
                                 ", ctx: " + context + ")");
            s_lastReportedDestroyed = s_totalDestroyed.load();
            return;
        }

        // For bulk batch operations (loadDocument, searchSync), log in ranges of 200
        if (s_totalDestroyed % 200 == 0) {
            std::size_t start = s_lastReportedDestroyed + 1;
            std::size_t end = s_totalDestroyed.load();
            std::string rangeStr = (start < end) ? (std::to_string(start) + "-" + std::to_string(end))
                                                 : std::to_string(end);
            MemoryTelemetry::log("[PopplerLifetime] Page destroyed (live: " + std::to_string(s_livePages.load()) +
                                 ", total destroyed: " + rangeStr +
                                 ", ctx: " + context + ")");
            s_lastReportedDestroyed = s_totalDestroyed.load();
        }
    }

    static std::size_t getLivePages() {
        return s_livePages.load();
    }

    static std::size_t getTotalCreated() {
        return s_totalCreated.load();
    }

    static std::size_t getTotalDestroyed() {
        return s_totalDestroyed.load();
    }

    static void dump(const std::string& tag) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_totalDestroyed > s_lastReportedDestroyed) {
            std::size_t start = s_lastReportedDestroyed + 1;
            std::size_t end = s_totalDestroyed.load();
            std::string rangeStr = (start < end) ? (std::to_string(start) + "-" + std::to_string(end))
                                                 : std::to_string(end);
            MemoryTelemetry::log("[PopplerLifetime] Page destroyed (live: " + std::to_string(s_livePages.load()) +
                                 ", total destroyed: " + rangeStr +
                                 ", ctx: " + tag + ")");
            s_lastReportedDestroyed = s_totalDestroyed.load();
        }

        MemoryTelemetry::log("[PopplerLifetime] === " + tag + " === Live Pages: " +
                             std::to_string(s_livePages.load()) +
                             " | Total Created: " + std::to_string(s_totalCreated.load()) +
                             " | Total Destroyed: " + std::to_string(s_totalDestroyed.load()) +
                             " | Process Private Bytes: " +
                             MemoryTelemetry::formatMB(MemoryTelemetry::getProcessPrivateBytes()) +
                             " | WS: " + MemoryTelemetry::formatMB(MemoryTelemetry::getProcessWorkingSet()));
    }

private:
    static inline std::mutex s_mutex;
    static inline std::atomic<std::size_t> s_livePages{0};
    static inline std::atomic<std::size_t> s_totalCreated{0};
    static inline std::atomic<std::size_t> s_totalDestroyed{0};
    static inline std::size_t s_lastReportedDestroyed{0};
};

} // namespace FluidCoreApp
