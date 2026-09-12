#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <poppler.h>
#include "MemoryTelemetry.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#include <dbghelp.h>
#endif

#ifdef FLUIDCORE_HAS_MIMALLOC
#include <mimalloc.h>
#endif

namespace {

#ifdef _WIN32
static HANDLE g_trackerHeap = nullptr;
#endif

struct MemSample {
    std::size_t privateBytes = 0;
    std::size_t workingSet = 0;
    std::size_t heapAllocated = 0;
    std::size_t heapCommitted = 0;
    std::size_t trackerHeapAllocated = 0;
    std::vector<std::size_t> heapBreakdown;
};

MemSample sampleMemory() {
    MemSample s;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        s.privateBytes = pmc.PrivateUsage;
        s.workingSet = pmc.WorkingSetSize;
    }

    DWORD numHeaps = GetProcessHeaps(0, nullptr);
    if (numHeaps > 0) {
        std::vector<HANDLE> heaps(numHeaps);
        DWORD actual = GetProcessHeaps(numHeaps, heaps.data());
        for (DWORD i = 0; i < actual; ++i) {
            HEAP_SUMMARY summary = {};
            summary.cb = sizeof(HEAP_SUMMARY);
            if (HeapSummary(heaps[i], 0, &summary)) {
                if (g_trackerHeap && heaps[i] == g_trackerHeap) {
                    s.trackerHeapAllocated += summary.cbAllocated;
                    continue;
                }
                s.heapAllocated += summary.cbAllocated;
                s.heapCommitted += summary.cbCommitted;
                s.heapBreakdown.push_back(summary.cbAllocated);
            }
        }
    }
#endif
    return s;
}

std::string formatMB(std::size_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    return oss.str();
}

std::string formatSignedMB(long long delta) {
    std::string sign = delta >= 0 ? "+" : "-";
    std::size_t absD = delta >= 0 ? delta : -delta;
    return sign + formatMB(absD);
}

void printCheckpoint(const std::string& label, const MemSample& current, const MemSample& base) {
    long long deltaPriv = static_cast<long long>(current.privateBytes) - static_cast<long long>(base.privateBytes);
    long long deltaAlloc = static_cast<long long>(current.heapAllocated) - static_cast<long long>(base.heapAllocated);
    std::ostringstream ss;
    ss << "[Checkpoint] " << std::left << std::setw(38) << label 
       << " | Priv: " << std::setw(9) << formatMB(current.privateBytes)
       << " (" << std::setw(10) << formatSignedMB(deltaPriv) << ")"
       << " | LiveAlloc: " << std::setw(9) << formatMB(current.heapAllocated)
       << " (" << std::setw(10) << formatSignedMB(deltaAlloc) << ")"
       << " | HeapCommit: " << formatMB(current.heapCommitted);
    if (current.trackerHeapAllocated > 0) {
        ss << " | TrackerHeap: " << formatMB(current.trackerHeapAllocated);
    }
    if (!current.heapBreakdown.empty()) {
        ss << " | Heaps: [";
        for (size_t i = 0; i < current.heapBreakdown.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "#" << i << ":" << formatMB(current.heapBreakdown[i]);
        }
        ss << "]";
    }
    FluidCoreApp::MemoryTelemetry::log(ss.str());
}

std::string pathToUri(const std::string& path) {
    GFile* file = g_file_new_for_path(path.c_str());
    char* uri = g_file_get_uri(file);
    std::string result(uri);
    g_free(uri);
    g_object_unref(file);
    return result;
}

struct SearchStats {
    int pageCount = 0;
    int hitCount = 0;
    double elapsedSeconds = 0.0;
};

SearchStats performSearch(PopplerDocument* doc, const std::string& query) {
    SearchStats stats;
    auto t0 = std::chrono::high_resolution_clock::now();
    int nPages = poppler_document_get_n_pages(doc);
    stats.pageCount = nPages;

    for (int i = 0; i < nPages; ++i) {
        PopplerPage* page = poppler_document_get_page(doc, i);
        if (!page) continue;

        GList* matches = poppler_page_find_text_with_options(page, query.c_str(), POPPLER_FIND_DEFAULT);
        for (GList* l = matches; l != nullptr; l = l->next) {
            stats.hitCount++;
            poppler_rectangle_free(static_cast<PopplerRectangle*>(l->data));
        }
        if (matches) {
            g_list_free(matches);
        }
        g_object_unref(page);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    stats.elapsedSeconds = std::chrono::duration<double>(t1 - t0).count();
    return stats;
}

void runPersistentProtocol(const std::string& pdfPath) {
    std::cout << "\n========================================================================================\n";
    std::cout << "  PROTOCOL 1: PERSISTENT DOCUMENT SEARCH (Current Baseline Architecture)\n";
    std::cout << "  All searches executed against the persistent GUI PopplerDocument*.\n";
    std::cout << "========================================================================================\n";

    std::string uri = pathToUri(pdfPath);
    GError* err = nullptr;
    PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), nullptr, &err);
    if (!doc) {
        std::cerr << "Failed to open document: " << (err ? err->message : "unknown error") << "\n";
        if (err) g_error_free(err);
        return;
    }

    MemSample base = sampleMemory();
    printCheckpoint("1. Baseline: Document Loaded", base, base);

    // Step 1a: Get pages only (Pass 1 - populates Catalog::pages)
    std::cout << "  --> Step 1a: Traversing get_page() on 892 pages (NO SEARCH, Catalog page-tree population)...\n";
    int nPages = poppler_document_get_n_pages(doc);
    for (int i = 0; i < nPages; ++i) {
        PopplerPage* p = poppler_document_get_page(doc, i);
        if (p) g_object_unref(p);
    }
    MemSample sPageOnly1 = sampleMemory();
    printCheckpoint("1a. Post-get_page() Pass 1 (Catalog populated)", sPageOnly1, base);

    // Step 1c: Get text only Pass 1 (Text extraction without findText)
    std::cout << "  --> Step 1c: Traversing get_text() on 892 pages (Text extraction only, NO findText)...\n";
    for (int i = 0; i < nPages; ++i) {
        PopplerPage* p = poppler_document_get_page(doc, i);
        if (p) {
            char* t = poppler_page_get_text(p);
            g_free(t);
            g_object_unref(p);
        }
    }
    MemSample sTextOnly1 = sampleMemory();
    printCheckpoint("1c. Post-get_text() Pass 1 (Text extraction only)", sTextOnly1, base);

    // Step 1d: Get text only Pass 2 (Repeated text extraction)
    std::cout << "  --> Step 1d: Traversing get_text() Pass 2 (Repeated text extraction)...\n";
    for (int i = 0; i < nPages; ++i) {
        PopplerPage* p = poppler_document_get_page(doc, i);
        if (p) {
            char* t = poppler_page_get_text(p);
            g_free(t);
            g_object_unref(p);
        }
    }
    MemSample sTextOnly2 = sampleMemory();
    printCheckpoint("1d. Post-get_text() Pass 2 (Repeated text extraction)", sTextOnly2, base);

    // Search 1: "futures"
    std::cout << "  --> Running Search 1 ('futures') across 892 pages on persistent doc...\n";
    auto s1Stats = performSearch(doc, "futures");
    MemSample s1 = sampleMemory();
    printCheckpoint("2. Post-Search 1 ('futures', hits: " + std::to_string(s1Stats.hitCount) + ")", s1, base);
    _heapmin();
    MemSample h1 = sampleMemory();
    printCheckpoint("3. Post-HeapMin 1", h1, base);

    // Search 2: "futures" (Identical query repetition)
    std::cout << "  --> Running Search 2 (REPEATED 'futures') across 892 pages...\n";
    auto s2Stats = performSearch(doc, "futures");
    MemSample s2 = sampleMemory();
    printCheckpoint("4. Post-Search 2 (REPEATED 'futures', hits: " + std::to_string(s2Stats.hitCount) + ")", s2, base);
    _heapmin();
    MemSample h2 = sampleMemory();
    printCheckpoint("5. Post-HeapMin 2", h2, base);

    // Search 3: "futures" (3rd repetition)
    std::cout << "  --> Running Search 3 (REPEATED 'futures') across 892 pages...\n";
    auto s3Stats = performSearch(doc, "futures");
    MemSample s3 = sampleMemory();
    printCheckpoint("6. Post-Search 3 (REPEATED 'futures', hits: " + std::to_string(s3Stats.hitCount) + ")", s3, base);

    // Search 4: "derivatives" (New query)
    std::cout << "  --> Running Search 4 (NEW QUERY 'derivatives') across 892 pages...\n";
    auto s4Stats = performSearch(doc, "derivatives");
    MemSample s4 = sampleMemory();
    printCheckpoint("7. Post-Search 4 (NEW 'derivatives', hits: " + std::to_string(s4Stats.hitCount) + ")", s4, base);

    // Teardown document
    g_object_unref(doc);
    MemSample postClose = sampleMemory();
    printCheckpoint("7. Post-Close: Document Destroyed", postClose, base);
    _heapmin();
    MemSample postCloseHeap = sampleMemory();
    printCheckpoint("8. Final HeapMin", postCloseHeap, base);

    std::cout << "\n  Summary for Persistent Architecture:\n";
    std::cout << "    - Search 1 Delta:          " << formatSignedMB(static_cast<long long>(s1.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
    std::cout << "    - Search 2 Delta:          " << formatSignedMB(static_cast<long long>(s2.privateBytes) - static_cast<long long>(s1.privateBytes)) << "\n";
    std::cout << "    - Search 3 Delta:          " << formatSignedMB(static_cast<long long>(s3.privateBytes) - static_cast<long long>(s2.privateBytes)) << "\n";
    std::cout << "    - Net Retained at Search 3:" << formatSignedMB(static_cast<long long>(s3.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
    std::cout << "    - Retained After Teardown: " << formatSignedMB(static_cast<long long>(postCloseHeap.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
}

void runEphemeralProtocol(const std::string& pdfPath) {
    std::cout << "\n========================================================================================\n";
    std::cout << "  PROTOCOL 2: EPHEMERAL DOCUMENT SEARCH (Proposed Architecture)\n";
    std::cout << "  Persistent GUI PopplerDocument* remains untouched.\n";
    std::cout << "  Worker opens throwaway PopplerDocument*, searches, then destroys it immediately.\n";
    std::cout << "========================================================================================\n";

    std::string uri = pathToUri(pdfPath);
    GError* err = nullptr;
    PopplerDocument* guiDoc = poppler_document_new_from_file(uri.c_str(), nullptr, &err);
    if (!guiDoc) {
        std::cerr << "Failed to open document: " << (err ? err->message : "unknown error") << "\n";
        if (err) g_error_free(err);
        return;
    }

    MemSample base = sampleMemory();
    printCheckpoint("1. Baseline: GUI Document Loaded", base, base);

    // Search 1: "futures"
    std::cout << "  --> Running Search 1 ('futures') on Ephemeral searchDoc1...\n";
    PopplerDocument* sDoc1 = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    auto s1Stats = performSearch(sDoc1, "futures");
    MemSample s1_active = sampleMemory();
    printCheckpoint("2a. Search 1 Complete (searchDoc1 alive)", s1_active, base);
    g_object_unref(sDoc1);
    MemSample s1_teardown = sampleMemory();
    printCheckpoint("2b. Post-Unref searchDoc1 (Destroyed)", s1_teardown, base);
    _heapmin();
    MemSample h1 = sampleMemory();
    printCheckpoint("2c. Post-HeapMin 1", h1, base);

    // Search 2: "derivatives"
    std::cout << "  --> Running Search 2 ('derivatives') on Ephemeral searchDoc2...\n";
    PopplerDocument* sDoc2 = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    auto s2Stats = performSearch(sDoc2, "derivatives");
    MemSample s2_active = sampleMemory();
    printCheckpoint("3a. Search 2 Complete (searchDoc2 alive)", s2_active, base);
    g_object_unref(sDoc2);
    MemSample s2_teardown = sampleMemory();
    printCheckpoint("3b. Post-Unref searchDoc2 (Destroyed)", s2_teardown, base);
    _heapmin();
    MemSample h2 = sampleMemory();
    printCheckpoint("3c. Post-HeapMin 2", h2, base);

    // Search 3: "options"
    std::cout << "  --> Running Search 3 ('options') on Ephemeral searchDoc3...\n";
    PopplerDocument* sDoc3 = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    auto s3Stats = performSearch(sDoc3, "options");
    MemSample s3_active = sampleMemory();
    printCheckpoint("4a. Search 3 Complete (searchDoc3 alive)", s3_active, base);
    g_object_unref(sDoc3);
    MemSample s3_teardown = sampleMemory();
    printCheckpoint("4b. Post-Unref searchDoc3 (Destroyed)", s3_teardown, base);
    _heapmin();
    MemSample h3 = sampleMemory();
    printCheckpoint("4c. Post-HeapMin 3", h3, base);

    // Teardown GUI document
    g_object_unref(guiDoc);
    MemSample postGuiClose = sampleMemory();
    printCheckpoint("5. Post-Close: GUI Document Destroyed", postGuiClose, base);
    _heapmin();
    MemSample postCloseHeap = sampleMemory();
    printCheckpoint("6. Final HeapMin", postCloseHeap, base);

    std::cout << "\n  Summary for Ephemeral Architecture:\n";
    std::cout << "    - Search 1 Active Delta:   " << formatSignedMB(static_cast<long long>(s1_active.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
    std::cout << "    - Search 1 Teardown Delta: " << formatSignedMB(static_cast<long long>(s1_teardown.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
    std::cout << "    - Search 2 Teardown Delta: " << formatSignedMB(static_cast<long long>(s2_teardown.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
    std::cout << "    - Search 3 Teardown Delta: " << formatSignedMB(static_cast<long long>(s3_teardown.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
    std::cout << "    - Net Retained vs Base:    " << formatSignedMB(static_cast<long long>(s3_teardown.privateBytes) - static_cast<long long>(base.privateBytes)) << "\n";
}

#ifdef _WIN32
typedef void* (*malloc_fn)(size_t);
typedef void (*free_fn)(void*);
typedef void* (*calloc_fn)(size_t, size_t);
typedef void* (*realloc_fn)(void*, size_t);

static malloc_fn g_orig_malloc = nullptr;
static free_fn g_orig_free = nullptr;
static calloc_fn g_orig_calloc = nullptr;
static realloc_fn g_orig_realloc = nullptr;

static thread_local bool t_inHook = false;
static volatile bool g_trackingActive = false;
static volatile uint8_t g_currentPassId = 0;

#define MAX_STACK_DEPTH 4

struct TrackerRecord {
    void* ptr;
    size_t size;
    uint8_t passId;
    USHORT stackDepth;
    void* stack[MAX_STACK_DEPTH];
    TrackerRecord* next;
};

#define TRACKER_BUCKET_COUNT 262144
static TrackerRecord** g_buckets = nullptr;
static CRITICAL_SECTION g_trackerLock;
static bool g_optionBActive = false;
static uintptr_t g_winpthreadBase = 0;
static uintptr_t g_winpthreadEnd = 0;

static void trackerInit() {
    if (!g_trackerHeap) {
        g_trackerHeap = HeapCreate(0, 0, 0);
        InitializeCriticalSection(&g_trackerLock);
        g_buckets = reinterpret_cast<TrackerRecord**>(HeapAlloc(g_trackerHeap, HEAP_ZERO_MEMORY, sizeof(TrackerRecord*) * TRACKER_BUCKET_COUNT));

        HMODULE hPthread = GetModuleHandleA("libwinpthread-1.dll");
        if (!hPthread) {
            hPthread = LoadLibraryA("libwinpthread-1.dll");
        }
        if (hPthread) {
            MODULEINFO mi = {};
            if (GetModuleInformation(GetCurrentProcess(), hPthread, &mi, sizeof(mi))) {
                g_winpthreadBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                g_winpthreadEnd = g_winpthreadBase + mi.SizeOfImage;
            }
        }
    }
}

static inline uint32_t trackerHash(void* p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    v = (v >> 4) ^ (v >> 9) ^ (v >> 16);
    return static_cast<uint32_t>(v % TRACKER_BUCKET_COUNT);
}

static size_t g_passAllocBytes[4] = {0, 0, 0, 0};
static size_t g_passAllocCount[4] = {0, 0, 0, 0};
static size_t g_passFreedBytes[4] = {0, 0, 0, 0};
static size_t g_passFreedCount[4] = {0, 0, 0, 0};
static size_t g_crossPassFreedBytes[4][4] = {};
static size_t g_untrackedFreesCount[4] = {0, 0, 0, 0};

static inline void trackerRecordAlloc(void* ptr, size_t sz) {
    if (!ptr || !g_trackerHeap || !g_trackingActive) return;
    if (g_currentPassId >= 1 && g_currentPassId <= 3) {
        g_passAllocBytes[g_currentPassId] += sz;
        g_passAllocCount[g_currentPassId]++;
    }
    void* frames[MAX_STACK_DEPTH + 2];
    USHORT captured = RtlCaptureStackBackTrace(2, MAX_STACK_DEPTH + 2, frames, nullptr);
    
    EnterCriticalSection(&g_trackerLock);
    uint32_t b = trackerHash(ptr);
    TrackerRecord* rec = static_cast<TrackerRecord*>(HeapAlloc(g_trackerHeap, 0, sizeof(TrackerRecord)));
    rec->ptr = ptr;
    rec->size = sz;
    rec->passId = g_currentPassId;
    USHORT depth = captured > MAX_STACK_DEPTH ? MAX_STACK_DEPTH : captured;
    rec->stackDepth = depth;
    for (USHORT i = 0; i < depth; ++i) {
        rec->stack[i] = frames[i];
    }
    rec->next = g_buckets[b];
    g_buckets[b] = rec;
    LeaveCriticalSection(&g_trackerLock);
}

static inline void trackerRecordFree(void* ptr) {
    if (!ptr || !g_trackerHeap || !g_trackingActive) return;
    EnterCriticalSection(&g_trackerLock);
    uint32_t b = trackerHash(ptr);
    TrackerRecord** curr = &g_buckets[b];
    while (*curr) {
        if ((*curr)->ptr == ptr) {
            TrackerRecord* toFree = *curr;
            *curr = toFree->next;
            uint8_t origPid = toFree->passId;
            size_t sz = toFree->size;
            if (g_currentPassId >= 1 && g_currentPassId <= 3) {
                if (origPid == g_currentPassId) {
                    g_passFreedBytes[g_currentPassId] += sz;
                    g_passFreedCount[g_currentPassId]++;
                } else if (origPid >= 1 && origPid <= 3) {
                    g_crossPassFreedBytes[g_currentPassId][origPid] += sz;
                }
            }
            HeapFree(g_trackerHeap, 0, toFree);
            LeaveCriticalSection(&g_trackerLock);
            return;
        }
        curr = &(*curr)->next;
    }
    if (g_currentPassId >= 1 && g_currentPassId <= 3) {
        g_untrackedFreesCount[g_currentPassId]++;
    }
    LeaveCriticalSection(&g_trackerLock);
}

static void* my_hook_malloc(size_t sz) {
    if (t_inHook || !g_trackingActive) return g_orig_malloc ? g_orig_malloc(sz) : malloc(sz);
    t_inHook = true;
    void* p = g_orig_malloc(sz);
    if (p) trackerRecordAlloc(p, sz);
    t_inHook = false;
    return p;
}

static size_t cleanupOptionBMutexes(int passId) {
    if (passId < 1 || passId > 3 || !g_trackerHeap || !g_buckets || g_winpthreadBase == 0) return 0;
    size_t count = 0;
    t_inHook = true;
    EnterCriticalSection(&g_trackerLock);
    for (int i = 0; i < TRACKER_BUCKET_COUNT; ++i) {
        TrackerRecord** curr = &g_buckets[i];
        while (*curr) {
            TrackerRecord* rec = *curr;
            if (rec->passId == passId && rec->size == 24 && rec->stackDepth > 0) {
                bool isPthread = false;
                for (USHORT s = 0; s < rec->stackDepth; ++s) {
                    uintptr_t caller = reinterpret_cast<uintptr_t>(rec->stack[s]);
                    if (caller >= g_winpthreadBase && caller < g_winpthreadEnd) {
                        isPthread = true;
                        break;
                    }
                }
                if (isPthread) {
                    void* p = rec->ptr;
                    *curr = rec->next;
                    g_passFreedBytes[passId] += rec->size;
                    g_passFreedCount[passId]++;
                    HeapFree(g_trackerHeap, 0, rec);
                    if (g_orig_free) g_orig_free(p);
                    else free(p);
                    count++;
                    continue;
                }
            }
            curr = &(*curr)->next;
        }
    }
    LeaveCriticalSection(&g_trackerLock);
    t_inHook = false;
#ifdef FLUIDCORE_HAS_MIMALLOC
    mi_collect(true);
#endif
    _heapmin();
    return count;
}

static void* my_hook_calloc(size_t num, size_t sz) {
    if (t_inHook || !g_trackingActive) return g_orig_calloc ? g_orig_calloc(num, sz) : calloc(num, sz);
    t_inHook = true;
    void* p = g_orig_calloc(num, sz);
    if (p) trackerRecordAlloc(p, num * sz);
    t_inHook = false;
    return p;
}

static void* my_hook_realloc(void* ptr, size_t sz) {
    if (t_inHook || !g_trackingActive) return g_orig_realloc ? g_orig_realloc(ptr, sz) : realloc(ptr, sz);
    t_inHook = true;
    void* p = g_orig_realloc(ptr, sz);
    if (p) {
        if (ptr) trackerRecordFree(ptr);
        if (sz > 0) trackerRecordAlloc(p, sz);
    }
    t_inHook = false;
    return p;
}

static void my_hook_free(void* p) {
    if (t_inHook || !g_trackingActive) {
        if (g_orig_free) g_orig_free(p);
        else free(p);
        return;
    }
    t_inHook = true;
    trackerRecordFree(p);
    g_orig_free(p);
    t_inHook = false;
}

static void hookModuleIAT(HMODULE hMod) {
    if (!hMod) return;
    BYTE* base = reinterpret_cast<BYTE*>(hMod);
    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    
    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return;
    
    PIMAGE_IMPORT_DESCRIPTOR importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + importRva);
    for (; importDesc->Name != 0; ++importDesc) {
        PIMAGE_THUNK_DATA thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + importDesc->FirstThunk);
        PIMAGE_THUNK_DATA origThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
        
        for (; thunk->u1.Function != 0; ++thunk, ++origThunk) {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
            PIMAGE_IMPORT_BY_NAME importByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + origThunk->u1.AddressOfData);
            if (strcmp(importByName->Name, "malloc") == 0) {
                if (!g_orig_malloc) g_orig_malloc = reinterpret_cast<malloc_fn>(thunk->u1.Function);
                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(my_hook_malloc);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
            } else if (strcmp(importByName->Name, "calloc") == 0) {
                if (!g_orig_calloc) g_orig_calloc = reinterpret_cast<calloc_fn>(thunk->u1.Function);
                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(my_hook_calloc);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
            } else if (strcmp(importByName->Name, "realloc") == 0) {
                if (!g_orig_realloc) g_orig_realloc = reinterpret_cast<realloc_fn>(thunk->u1.Function);
                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(my_hook_realloc);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
            } else if (strcmp(importByName->Name, "free") == 0) {
                if (!g_orig_free) g_orig_free = reinterpret_cast<free_fn>(thunk->u1.Function);
                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(my_hook_free);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
            }
        }
    }
}

static void installAllocationHooks() {
    trackerInit();
    HMODULE hMods[1024];
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded;
    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        size_t count = cbNeeded / sizeof(HMODULE);
        for (size_t i = 0; i < count; ++i) {
            hookModuleIAT(hMods[i]);
        }
    }
}

struct ModuleRange {
    std::string name;
    std::string fullPath;
    uintptr_t base = 0;
    uintptr_t end = 0;
};

static std::vector<ModuleRange> getLoadedModules() {
    std::vector<ModuleRange> list;
    HMODULE hMods[1024];
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded;
    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        size_t count = cbNeeded / sizeof(HMODULE);
        for (size_t i = 0; i < count; ++i) {
            MODULEINFO info = {};
            if (GetModuleInformation(hProcess, hMods[i], &info, sizeof(info))) {
                char path[MAX_PATH] = {};
                GetModuleFileNameA(hMods[i], path, sizeof(path));
                const char* baseName = strrchr(path, '\\');
                if (!baseName) baseName = strrchr(path, '/');
                baseName = baseName ? baseName + 1 : path;
                ModuleRange r;
                r.name = baseName;
                r.fullPath = path;
                r.base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
                r.end = r.base + info.SizeOfImage;
                list.push_back(r);
            }
        }
    }
    return list;
}

static const ModuleRange* findModule(const std::vector<ModuleRange>& modules, void* addr) {
    uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (const auto& m : modules) {
        if (a >= m.base && a < m.end) return &m;
    }
    return nullptr;
}

static const ModuleRange* resolveEffectiveCaller(const std::vector<ModuleRange>& modules,
                                                 void* const* stack, USHORT depth, void*& outCallerAddr) {
    outCallerAddr = nullptr;
    const ModuleRange* best = nullptr;
    for (USHORT i = 0; i < depth; ++i) {
        const ModuleRange* m = findModule(modules, stack[i]);
        if (!m) continue;
        if (_stricmp(m->name.c_str(), "ucrtbase.dll") != 0 &&
            _strnicmp(m->name.c_str(), "api-ms-win", 10) != 0 &&
            _stricmp(m->name.c_str(), "ntdll.dll") != 0 &&
            _stricmp(m->name.c_str(), "kernel32.dll") != 0 &&
            _stricmp(m->name.c_str(), "KernelBase.dll") != 0) {
            outCallerAddr = stack[i];
            return m;
        }
        if (!best) best = m;
    }
    return best;
}

void runAttributionProtocol(const std::string& pdfPath, bool enableOptionB = false) {
    std::cout << "\n========================================================================================\n";
    if (enableOptionB) {
        std::cout << "  PROTOCOL 4: OPTION B (MIMALLOC + WINPTHREAD MUTEX REMEDIATION PROBE)\n";
        std::cout << "  Hooks IAT to track and reclaim orphaned 24-byte mutexes post-search.\n";
    } else {
        std::cout << "  PROTOCOL 3: MULTI-PASS MODULE ATTRIBUTION (Option A Differential Testing)\n";
        std::cout << "  Hooks IAT malloc/calloc/realloc/free to attribute retained LiveAlloc.\n";
    }
    std::cout << "  Runs 3 back-to-back ephemeral searches to separate one-time init from recurring leaks.\n";
    std::cout << "========================================================================================\n";

    g_optionBActive = enableOptionB;
    installAllocationHooks();

    std::string uri = pathToUri(pdfPath);
    GError* err = nullptr;
    PopplerDocument* guiDoc = poppler_document_new_from_file(uri.c_str(), nullptr, &err);
    if (!guiDoc) {
        std::cerr << "Failed to open document: " << (err ? err->message : "unknown error") << "\n";
        if (err) g_error_free(err);
        return;
    }

    MemSample base = sampleMemory();
    printCheckpoint("1. Baseline: GUI Document Loaded", base, base);

    auto printPassDiagnostics = [](int pid) {
        std::ostringstream ss;
        ss << "    [Pass " << pid << " Flow] Alloc: " << formatMB(g_passAllocBytes[pid]) 
           << " (" << g_passAllocCount[pid] << ") | Freed Self: " << formatMB(g_passFreedBytes[pid])
           << " (" << g_passFreedCount[pid] << ") | Cross-Pass Freed: "
           << formatMB(g_crossPassFreedBytes[pid][1] + g_crossPassFreedBytes[pid][2] + g_crossPassFreedBytes[pid][3])
           << " | Untracked Frees: " << g_untrackedFreesCount[pid];
        FluidCoreApp::MemoryTelemetry::log(ss.str());
    };

    // Pass 1: "futures"
    std::cout << "\n  --> Running Pass 1: Ephemeral Search ('futures') across 892 pages...\n" << std::flush;
    g_currentPassId = 1;
    g_trackingActive = true;
    PopplerDocument* sDoc1 = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    auto s1Stats = performSearch(sDoc1, "futures");
    g_object_unref(sDoc1);
    if (enableOptionB) {
        size_t c1 = cleanupOptionBMutexes(1);
        std::cout << "    [Option B Cleanup] Reclaimed " << c1 << " orphaned winpthread mutex handles (" << formatMB(c1 * 24) << ")\n" << std::flush;
    }
    g_trackingActive = false;
    MemSample s1 = sampleMemory();
    printCheckpoint("  Pass 1 Complete (hits: " + std::to_string(s1Stats.hitCount) + ")", s1, base);
    printPassDiagnostics(1);

    // Pass 2: "derivatives"
    std::cout << "\n  --> Running Pass 2: Ephemeral Search ('derivatives') across 892 pages...\n" << std::flush;
    g_currentPassId = 2;
    g_trackingActive = true;
    PopplerDocument* sDoc2 = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    auto s2Stats = performSearch(sDoc2, "derivatives");
    g_object_unref(sDoc2);
    if (enableOptionB) {
        size_t c2 = cleanupOptionBMutexes(2);
        std::cout << "    [Option B Cleanup] Reclaimed " << c2 << " orphaned winpthread mutex handles (" << formatMB(c2 * 24) << ")\n" << std::flush;
    }
    g_trackingActive = false;
    MemSample s2 = sampleMemory();
    printCheckpoint("  Pass 2 Complete (hits: " + std::to_string(s2Stats.hitCount) + ")", s2, base);
    printPassDiagnostics(2);

    // Pass 3: "options"
    std::cout << "\n  --> Running Pass 3: Ephemeral Search ('options') across 892 pages...\n" << std::flush;
    g_currentPassId = 3;
    g_trackingActive = true;
    PopplerDocument* sDoc3 = poppler_document_new_from_file(uri.c_str(), nullptr, nullptr);
    auto s3Stats = performSearch(sDoc3, "options");
    g_object_unref(sDoc3);
    if (enableOptionB) {
        size_t c3 = cleanupOptionBMutexes(3);
        std::cout << "    [Option B Cleanup] Reclaimed " << c3 << " orphaned winpthread mutex handles (" << formatMB(c3 * 24) << ")\n" << std::flush;
    }
    g_trackingActive = false;
    MemSample s3 = sampleMemory();
    printCheckpoint("  Pass 3 Complete (hits: " + std::to_string(s3Stats.hitCount) + ")", s3, base);
    printPassDiagnostics(3);

    g_object_unref(guiDoc);
    MemSample finalMem = sampleMemory();
    printCheckpoint("  Post-Close: GUI Document Destroyed", finalMem, base);

    // Analyze survivors
    auto modules = getLoadedModules();
    HANDLE hProcess = GetCurrentProcess();
    SymInitialize(hProcess, nullptr, TRUE);

    // Per-pass per-module stats: passBytes[passId][moduleName]
    std::map<std::string, std::size_t> passBytes[4];
    std::map<std::string, std::size_t> passCount[4];
    std::map<std::string, std::map<void*, std::size_t>> passCallers[4];
    std::set<std::string> allSeenModules;
    std::size_t totalSurvivingBytes = 0;

    for (int i = 0; i < TRACKER_BUCKET_COUNT; ++i) {
        TrackerRecord* rec = g_buckets[i];
        while (rec) {
            uint8_t pid = rec->passId;
            if (pid >= 1 && pid <= 3) {
                void* callerAddr = nullptr;
                const ModuleRange* m = resolveEffectiveCaller(modules, rec->stack, rec->stackDepth, callerAddr);
                std::string modName = m ? m->name : "unknown";
                passBytes[pid][modName] += rec->size;
                passCount[pid][modName]++;
                if (callerAddr) {
                    passCallers[pid][modName][callerAddr] += rec->size;
                }
                allSeenModules.insert(modName);
                totalSurvivingBytes += rec->size;
            }
            rec = rec->next;
        }
    }

    std::ostringstream oss;
    oss << "\n==============================================================================================\n"
        << "  3-PASS MODULE ATTRIBUTION MATRIX (SURVIVING LIVEALLOC)\n"
        << "  Total Retained LiveAlloc Tracked: " << formatMB(totalSurvivingBytes) << "\n"
        << "==============================================================================================\n"
        << "  " << std::left << std::setw(28) << "Module"
        << " | " << std::setw(20) << "Pass 1 Retained"
        << " | " << std::setw(20) << "Pass 2 Retained"
        << " | " << std::setw(20) << "Pass 3 Retained"
        << " | Verdict\n"
        << "  " << std::string(28, '-') << "-+-" << std::string(20, '-') << "-+-" 
        << std::string(20, '-') << "-+-" << std::string(20, '-') << "-+-" << std::string(24, '-') << "\n";

    // Sort modules by total retained bytes across passes
    std::vector<std::pair<std::string, std::size_t>> sortedMods;
    for (const auto& mod : allSeenModules) {
        std::size_t sum = passBytes[1][mod] + passBytes[2][mod] + passBytes[3][mod];
        sortedMods.emplace_back(mod, sum);
    }
    std::sort(sortedMods.begin(), sortedMods.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::size_t totalPassBytes[4] = {0, 0, 0, 0};
    std::size_t totalPassAllocs[4] = {0, 0, 0, 0};

    std::vector<std::string> recurringModules;
    for (const auto& kv : sortedMods) {
        const std::string& mod = kv.first;
        std::size_t b1 = passBytes[1][mod];
        std::size_t b2 = passBytes[2][mod];
        std::size_t b3 = passBytes[3][mod];

        for (int p = 1; p <= 3; ++p) {
            totalPassBytes[p] += passBytes[p][mod];
            totalPassAllocs[p] += passCount[p][mod];
        }

        std::string verdict;
        if (b2 >= 100 * 1024 || b3 >= 100 * 1024) {
            verdict = "RECURRING LEAK SITE";
            recurringModules.push_back(mod);
        } else if (b1 >= 100 * 1024 && b2 < 100 * 1024 && b3 < 100 * 1024) {
            verdict = "One-Time Init";
        } else {
            verdict = "Negligible (<100 KB)";
        }

        auto fmtCell = [&](std::size_t b, std::size_t cnt) {
            std::string s = formatMB(b);
            if (cnt > 0) s += " (" + std::to_string(cnt) + ")";
            return s;
        };

        oss << "  " << std::left << std::setw(28) << mod
            << " | " << std::setw(20) << fmtCell(b1, passCount[1][mod])
            << " | " << std::setw(20) << fmtCell(b2, passCount[2][mod])
            << " | " << std::setw(20) << fmtCell(b3, passCount[3][mod])
            << " | " << verdict << "\n";
    }

    oss << "  " << std::string(28, '-') << "-+-" << std::string(20, '-') << "-+-" 
        << std::string(20, '-') << "-+-" << std::string(20, '-') << "-+-" << std::string(24, '-') << "\n";
    oss << "  " << std::left << std::setw(28) << "TOTAL ATTRIBUTED"
        << " | " << std::setw(20) << formatMB(totalPassBytes[1])
        << " | " << std::setw(20) << formatMB(totalPassBytes[2])
        << " | " << std::setw(20) << formatMB(totalPassBytes[3])
        << " |\n";
    oss << "  " << std::left << std::setw(28) << "SURVIVING ALLOC COUNT"
        << " | " << std::setw(20) << totalPassAllocs[1]
        << " | " << std::setw(20) << totalPassAllocs[2]
        << " | " << std::setw(20) << totalPassAllocs[3]
        << " |\n";
    oss << "  " << std::left << std::setw(28) << "CALC TRACKER OVERHEAD (64B)"
        << " | " << std::setw(20) << formatMB(totalPassAllocs[1] * sizeof(TrackerRecord))
        << " | " << std::setw(20) << formatMB(totalPassAllocs[2] * sizeof(TrackerRecord))
        << " | " << std::setw(20) << formatMB(totalPassAllocs[3] * sizeof(TrackerRecord))
        << " |\n";

    if (!recurringModules.empty()) {
        oss << "\n==============================================================================================\n"
            << "  TOP CALL SITES FOR RECURRING LEAK SITES (Pass 2 + Pass 3 Survivors)\n"
            << "==============================================================================================\n";

        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        for (const auto& mod : recurringModules) {
            oss << "\n  [" << mod << "]:\n";
            // Combine callers across pass 2 and pass 3
            std::map<void*, std::size_t> combinedCallers;
            for (const auto& c : passCallers[2][mod]) combinedCallers[c.first] += c.second;
            for (const auto& c : passCallers[3][mod]) combinedCallers[c.first] += c.second;

            std::vector<std::pair<void*, std::size_t>> sortedCallers(combinedCallers.begin(), combinedCallers.end());
            std::sort(sortedCallers.begin(), sortedCallers.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });

            int top = 0;
            for (const auto& sc : sortedCallers) {
                if (++top > 5) break;
                void* addr = sc.first;
                const ModuleRange* m = findModule(modules, addr);
                uintptr_t rva = m ? (reinterpret_cast<uintptr_t>(addr) - m->base) : 0;
                DWORD64 disp = 0;
                std::string symName = "";
                if (SymFromAddr(hProcess, reinterpret_cast<DWORD64>(addr), &disp, pSymbol)) {
                    symName = std::string(" (") + pSymbol->Name + "+0x" + std::to_string(disp) + ")";
                }
                oss << "    - " << std::setw(9) << formatMB(sc.second) 
                    << " retained at " << mod << "+0x" << std::hex << rva << std::dec << symName << "\n";
            }
        }
    }

    FluidCoreApp::MemoryTelemetry::log(oss.str());
    SymCleanup(hProcess);
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef FLUIDCORE_HAS_MIMALLOC
    mi_option_set(mi_option_purge_delay, 0);
    mi_option_set(mi_option_purge_decommits, 1);
#endif

    std::string defaultPdf = "D:/study material/FIN F414 - FRAM/FRAMTextbook.ltproj/documents/Hull J.C.-Options, Futures and Other Derivatives_9th edition.pdf";
    std::string mode = "all";
    std::string pdfPath = defaultPdf;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--persistent") {
            mode = "persistent";
        } else if (arg == "--ephemeral") {
            mode = "ephemeral";
        } else if (arg == "--attribute") {
            mode = "attribute";
        } else if (arg == "--option-b") {
            mode = "option-b";
        } else if (arg == "--all") {
            mode = "all";
        } else if (!arg.empty() && arg.rfind("--", 0) != 0) {
            pdfPath = arg;
        }
    }

    if (mode == "persistent") {
        runPersistentProtocol(pdfPath);
    } else if (mode == "ephemeral") {
        runEphemeralProtocol(pdfPath);
    } else if (mode == "attribute") {
#ifdef _WIN32
        runAttributionProtocol(pdfPath, false);
#else
        std::cerr << "--attribute is only supported on Windows.\n";
        return 1;
#endif
    } else if (mode == "option-b") {
#ifdef _WIN32
        runAttributionProtocol(pdfPath, true);
#else
        std::cerr << "--option-b is only supported on Windows.\n";
        return 1;
#endif
    } else {
        // Run both in separate child processes for 100% clean, unpolluted memory spaces
        std::string selfExe = argv[0];
        std::cout << "[Probe Harness] Spawning Protocol 1 in fresh process...\n";
        std::string cmd1 = "\"" + selfExe + "\" --persistent \"" + pdfPath + "\"";
        std::system(cmd1.c_str());

        std::cout << "\n[Probe Harness] Spawning Protocol 2 in fresh process...\n";
        std::string cmd2 = "\"" + selfExe + "\" --ephemeral \"" + pdfPath + "\"";
        std::system(cmd2.c_str());
    }

    return 0;
}

