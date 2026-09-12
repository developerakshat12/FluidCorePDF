# Context Index for `MEMORY_LEAK_INVESTIGATION_CHRONOLOGY_AND_TEST_CASES.md`

This document serves as an exact line-by-line navigation index and reference guide for [MEMORY_LEAK_INVESTIGATION_CHRONOLOGY_AND_TEST_CASES.md](file:///d:/FluidCorePDF/fluidcore-platform/debug/investigations/MEMORY_LEAK_INVESTIGATION_CHRONOLOGY_AND_TEST_CASES.md). Use the line ranges below to quickly locate specific technical topics, empirical benchmark tables, test case implementations, and architectural explanations.

---

## High-Level Document Map

| Line Range | Section / Topic | Core Subject Matter |
|---|---|---|
| **Lines 1–15** | [Section 1: Executive Summary & Scope](#section-1-executive-summary--scope-lines-115) | Core problem definition, target document (892 pages), Layer 1 vs. Layer 2 memory leak breakdown. |
| **Lines 17–80** | [Section 2: Chronological Investigation Phases](#section-2-chronological-investigation-phases-lines-1780) | Step-by-step chronology from initial 600–700 MB spikes to 20-pass production verification. |
| **Lines 82–512** | [Section 3: Comprehensive Catalog of All Test Cases](#section-3-comprehensive-catalog-of-all-test-cases-lines-82512) | 13 complete test suites with commands, implementation files, CLI flags, workflows, and empirical logs. |
| **Lines 514–526** | [Section 4: Key Architectural Learnings & Gotchas](#section-4-key-architectural-learnings--gotchas-lines-514526) | Technical gotchas: process heap vs. thread scope, GCC `<mutex>` bug, mimalloc mismatch, IAT GUI races. |
| **Lines 528–537** | [Section 5: Next Steps: Out-of-Process Search Worker](#section-5-next-steps-out-of-process-search-worker-lines-528537) | Architectural solution (`fluidcore_search_worker.exe`) to guarantee 0.00 MB GUI leak. |

---

## Detailed Line-by-Line Index

### Section 1: Executive Summary & Scope (Lines 1–15)
- **Lines 1–3**: Document title and executive summary heading.
- **Lines 4–8**: Scope and problem definition: 892-page textbook (*Hull J.C. Options, Futures and Other Derivatives 9th Edition*) spiking memory to 600–700 MB during search.
- **Lines 9–10**: **Layer 1 Leak Definition**: Hard recurring leak of **5.08 MB across 221,870 allocations of 24 bytes per search pass**, caused by MinGW-w64 GCC `libstdc++` `std::recursive_mutex::~recursive_mutex() = default;` omitting `pthread_mutex_destroy()`.
- **Lines 11–12**: **Layer 2 Heap Slack Definition**: 15–30 MB committed-but-unused virtual heap slack retained by Windows LFH and UCRT heap allocator.
- **Lines 13–15**: Document purpose and boundary demarcation.

---

### Section 2: Chronological Investigation Phases (Lines 17–80)
- **Lines 17–27**: Mermaid flowchart showing sequential flow from Phase 0 to Phase 6.
- **Lines 29–36**: **Phase 0: Initial Anomaly & Discovery**
  - Problems observed: 600–700 MB bloat during search.
  - Initial diagnostics: 4-second heartbeats (`FLUIDCORE_HEARTBEAT`), inner-loop `GetProcessMemoryInfo` queries (`s0`–`s4`), `PopplerLifetimeTracker`.
  - Root factors: unbounded `PageTileCache`, searching on GUI live `PopplerDocument*`, and Win32 sampling CPU stalls.
- **Lines 37–44**: **Phase 1: Architecture Decoupling & Hardening**
  - Code changes: Ephemeral search in [DocumentSearchService.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/services/DocumentSearchService.cpp), 300 ms debouncing in [SearchBarWidget.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/document/SearchBarWidget.cpp), LRU eviction in [PageTileCache.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/document/PageTileCache.cpp), telemetry gating in [MemoryTelemetry.h](file:///d:/FluidCorePDF/fluidcore-platform/src/app/services/MemoryTelemetry.h).
  - Outcome: Active search footprint reduced from 600–700 MB down to ~10–15 MB.
- **Lines 45–49**: **Phase 2: Baseline Differential Protocols**
  - Protocol 1 (persistent search) vs. Protocol 2 (ephemeral search) in [SearchIsolationProbe.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/tests/SearchIsolationProbe.cpp).
  - Identified that unreferencing ephemeral documents freed ~4.5 MB of Poppler C++ structures, isolating the remaining ~5.08 MB leak.
- **Lines 50–64**: **Phase 3: Option A (Deep Stack Attribution Engine)**
  - IAT hooking across DLLs on an isolated private heap (`HeapCreate`).
  - 3-pass test results: GLib clean (0.00 MB leak on passes 2 & 3); 100% of recurring allocations from `libwinpthread-1.dll!pthread_mutex_lock+0x88`.
  - Root cause disassembly in `poppler/Array.h` (L82) and `poppler/Dict.h` (L119).
- **Lines 65–71**: **Phase 4: Option B Remediation & Quarantined Probe Test**
  - Implementation of `cleanupOptionBMutexes()` in probe: 100.000% deallocation (0.00 MB surviving).
  - The production boundary: IAT hooking quarantined to avoid GTK3/Cairo/GIO race conditions.
- **Lines 72–80**: **Phase 5: Production Verification & Reality Check**
  - Why worker thread recycling failed to free process-scoped heap memory.
  - 20-pass benchmark summary (+5.08 MB/pass linear growth).

---

### Section 3: Comprehensive Catalog of All Test Cases (Lines 82–512)

| Test # | Test Name | Target / Mode | Line Range | Key Table / Log Output |
|:---:|---|---|:---:|---|
| **Test 1** | **Automated CTest Regression Suite** | 38 Test Targets | **Lines 84–139** | Full list of 38 tests, execution times, 100% pass (Lines 96–138) |
| **Test 2** | **Scenario A: Reading & Navigation** | `fluidcore_app.exe` | **Lines 141–164** | Step-by-step scrolling, zoom, card extraction, memory bounds (Lines 153–163) |
| **Test 3** | **Scenario B: Editing & Inking** | `fluidcore_app.exe` | **Lines 166–188** | 50 pen, 20 highlighter, 15 eraser, 20 undo/redo actions (Lines 178–187) |
| **Test 4** | **Scenario Reopen Audit** | `fluidcore_app.exe` | **Lines 190–211** | Open/close memory numbers, 0 live Poppler pages (Lines 202–209) |
| **Test 5** | **Probe Protocol 1: Persistent Search** | `search_isolation_probe.exe` | **Lines 213–240** | 3-pass persistent checkpoint table (+5.08 MB LiveAlloc/pass) (Lines 226–235) |
| **Test 6** | **Probe Protocol 2: Ephemeral Search** | `search_isolation_probe.exe` | **Lines 242–266** | SearchDoc unref checkpoint table (~4.5 MB reclaimed) (Lines 255–262) |
| **Test 7** | **Probe Protocol 3: Option A Attribution** | `search_isolation_probe.exe` | **Lines 268–310** | **3-Pass Module Attribution Matrix** & Call Sites (Lines 282–305) |
| **Test 8** | **Probe Protocol 4: Option B Probe** | `search_isolation_probe.exe` | **Lines 312–356** | Cleanup logs, 100.000% freed, 0.00 MB surviving matrix (Lines 326–351) |
| **Test 9** | **Production App 5-Pass Repeated Find** | `fluidcore_app.exe` | **Lines 358–381** | Summary of 5 passes (+5.00 MB to +5.10 MB per pass) (Lines 370–376) |
| **Test 10** | **Production App 20-Pass Benchmark** | `fluidcore_app.exe` | **Lines 383–425** | **Full 20-Pass Telemetry Table** (pass 1 to 20, thread recycles) (Lines 396–418) |
| **Test 11** | **Telemetry Gating Automated Test** | `MemoryTelemetry.h` | **Lines 427–442** | Gated byte verification: 0 bytes written vs. appended (Lines 437–440) |
| **Test 12** | **50-PDF Scalability Benchmark** | `scalability_benchmark_test.exe` | **Lines 444–459** | 50 diverse documents, 64 MB tile cache budget validation (Lines 452–458) |
| **Test 13** | **Brain Scratch Micro-Benchmarks** | Scratch isolated tests | **Lines 461–512** | 4 standalone micro-test analyses (Lines 464–512) |

#### Test Case 13 Sub-Index (Scratch Programs):
- **Lines 464–482**: **Micro-Test A (`test_thread_exit.cpp` / `run_thread_exit.ps1`)**
  - Tests whether `std::thread` termination reclaims winpthread allocations.
  - Console output showing growth from 6.84 MB $\to$ 46.50 MB $\to$ 71.20 MB $\to$ 95.80 MB.
  - Proves heap allocations are process-scoped in Windows UCRT.
- **Lines 483–498**: **Micro-Test B (`test_mimalloc.cpp` / `run_test.ps1`)**
  - Tests `mi_is_in_heap_region` vs. CRT `malloc`.
  - Explains allocator mismatch crash (`0xC0000374 STATUS_HEAP_CORRUPTION`) when `<mimalloc-new-delete.h>` was included in a single `.cpp`.
- **Lines 499–508**: **Micro-Test C (`test_retaddr.cpp` / `run_retaddr.ps1`)**
  - Tests GCC `__builtin_return_address` across MinGW shared library boundaries (`libwinpthread-1.dll`).
- **Lines 509–512**: **Micro-Test D (`run_debug.ps1`)**
  - PowerShell runner under MSYS2 UCRT64 environment.

---

### Section 4: Key Architectural Learnings & Gotchas (Lines 514–526)
- **Lines 517–518**: **Process Heap vs. Thread Local Scope**: Thread exit reclaims thread stack, kernel thread objects, and TLS, but does **not** free process heap blocks.
- **Lines 519–520**: **MinGW GCC libstdc++ `<mutex>` Defect**: `__GTHREAD_RECURSIVE_MUTEX_INIT` causes `~recursive_mutex() = default;`, skipping `pthread_mutex_destroy()`.
- **Lines 521–522**: **Allocator Mismatch Crash**: Why `<mimalloc-new-delete.h>` cannot be included in individual translation units when other libraries use CRT `free`.
- **Lines 523–524**: **Dynamic IAT Hooking Boundary**: Why IAT hooking causes race conditions in GTK3/Cairo/GIO GUI threads and must remain quarantined in test probes.

---

### Section 5: Next Steps: Out-of-Process Search Worker (Lines 528–537)
- **Lines 532–534**: Architecture of `fluidcore_search_worker.exe` (IPC via pipes/JSON over stdout).
- **Lines 535–537**: Why out-of-process execution guarantees **0.00 MB memory leak** in the GUI without any runtime hooks (the OS kernel reclaims 100% of heap and mutex handles upon process termination).

---

## Where to Look When You Need...

| If you are looking for... | Jump to Line Numbers |
|---|---|
| The exact 20-pass production memory growth table | **Lines 396–418** |
| The 3-pass Module Attribution Matrix (GLib vs. winpthread) | **Lines 282–300** |
| The disassembly/code location of the winpthread recursive_mutex leak | **Lines 60–64 & Lines 303–305** |
| The proof that Option B achieves 100.000% deallocation in the probe | **Lines 326–351** |
| Why worker thread recycling does NOT fix the leak in production | **Lines 73–78, Lines 422–424, & Lines 481–482** |
| The standalone scratch test showing thread exit memory retention | **Lines 464–482** |
| The cause of the `0xC0000374 STATUS_HEAP_CORRUPTION` crash | **Lines 76–77, Lines 496–498, & Lines 521–522** |
| The list of all 38 CTest targets and their pass status | **Lines 96–138** |
| The proposal for the out-of-process search worker | **Lines 532–537** |
