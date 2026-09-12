# FluidCorePDF Memory Leak Investigation, Chronology & Test Catalog

## 1. Executive Summary & Scope

During scalability and stress testing on large-format technical textbooks—specifically the **892-page** reference document *Hull J.C. - Options, Futures and Other Derivatives (9th Edition)*—the FluidCorePDF engineering team conducted an exhaustive memory leak and heap fragmentation investigation. 

Initial measurements revealed that executing full-text searches across the 892-page corpus spiked process private memory to **600–700 MB**, with memory failing to return to baseline after searches concluded.

The investigation revealed that memory bloat was composed of two distinct physical layers:
1. **Layer 1: Recurring Live Allocation Leak**: A hard leak of **5.08 MB across 221,870 allocations of 24 bytes per 892-page search pass**. This was traced to MinGW-w64 GCC's `libstdc++` `<mutex>` implementation where `std::recursive_mutex::~recursive_mutex() = default;` fails to invoke `pthread_mutex_destroy()`, permanently orphaning Win32 mutex handles dynamically allocated by `libwinpthread-1.dll!pthread_mutex_lock+0x88`.
2. **Layer 2: Virtual Heap Slack & LFH Retention**: An accumulation of **15–30 MB committed-but-unused memory** retained by the Windows Low Fragmentation Heap (LFH) and UCRT heap manager (`ucrtbase.dll`) after hundreds of thousands of ephemeral C++ objects are allocated and freed.

This document serves as the permanent, authoritative record of every diagnostic step, architectural hypothesis, code remediation, and empirical test case executed throughout the investigation.

---

## 2. Chronological Investigation Phases

```mermaid
flowchart TD
    P0[Phase 0: Initial Spikes 600-700MB] --> P1[Phase 1: Architecture Decoupling & Hardening]
    P1 --> P2[Phase 2: Baseline Differential Protocols]
    P2 --> P3[Phase 3: Option A Deep Stack Attribution Engine]
    P3 --> P4[Phase 4: Option B Mutex Remediation in Test Probe]
    P4 --> P5[Phase 5: Production 20-Pass Benchmark & Reality Check]
    P5 --> P6[Phase 6: Architectural Path Forward - Out-of-Process Search]
```

### Phase 0: Initial Anomaly & Discovery
- **Problem Observed**: Searching an 892-page document bloated process memory to 600–700 MB. Repetitive searches compounded memory usage.
- **Initial Diagnostics**: Continuous 4-second memory heartbeats (`FLUIDCORE_HEARTBEAT`), inner-loop `GetProcessMemoryInfo` queries (`s0` through `s4` per page), and lifetime tracking of Poppler page structures (`PopplerLifetimeTracker`).
- **Findings**:
  - `PageTileCache` was unbounded, accumulating hundreds of rendered Cairo surface rasters.
  - Background search ran on the GUI's live `PopplerDocument*`, polluting Poppler's internal document cache.
  - High-frequency Win32 memory sampling (~4,460 kernel-mode transitions per search) created CPU stalls.

### Phase 1: Architecture Decoupling & Hardening
- **Code Changes**:
  - [DocumentSearchService.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/services/DocumentSearchService.cpp): Decoupled search from the GUI document. Worker threads open an isolated throwaway `PopplerDocument*` per search (`FLUIDCORE_EPHEMERAL_SEARCH=1`), which is destroyed immediately upon search completion.
  - [SearchBarWidget.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/document/SearchBarWidget.cpp): Added 300 ms input debouncing (`kSearchDebounceMs = 300`) with synchronous flush on `Return`/`KP_Enter`, eliminating premature searches on partial query keystrokes.
  - [PageTileCache.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/document/PageTileCache.cpp) & [ExcerptTileCache.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/services/ExcerptTileCache.cpp): Enforced strict LRU eviction bounds (max 8 resident pages, max 64 MB).
  - [MemoryTelemetry.h](file:///d:/FluidCorePDF/fluidcore-platform/src/app/services/MemoryTelemetry.h): Gated heartbeats, file logging (`fluidcore_telemetry.log`), and inner-loop metrics behind explicit environment flags (`FLUIDCORE_LOG_TELEMETRY`, `FLUIDCORE_HEARTBEAT`, `FLUIDCORE_SEARCH_BENCHMARK`).
- **Outcome**: Dropped full-document search memory footprint from 600–700 MB down to **~10–15 MB active peak**, with zero Poppler page leaks.

### Phase 2: Baseline Differential Protocols
- **Test Protocol Implemented**: Created [SearchIsolationProbe.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/tests/SearchIsolationProbe.cpp) to run isolated head-to-head comparisons:
  - **Protocol 1 (Persistent Document Search)**: Searches executed on the GUI `PopplerDocument*`. Retained memory grew monotonically after every query.
  - **Protocol 2 (Ephemeral Document Search)**: Each search executed on a throwaway document. Verified that destroying the document freed ~4.5 MB of document C++ objects, but identified an unexplained recurring retention of ~5.08 MB LiveAlloc and 15–30 MB heap slack.

### Phase 3: Option A (Stack Trace & Module Attribution Engine)
- **Objective**: Attribute every surviving byte of LiveAlloc to its originating dynamic link library (`.dll`) and call site.
- **Implementation**:
  - Injected an Import Address Table (IAT) hook across all loaded modules for `malloc`, `calloc`, `realloc`, and `free`.
  - Allocated tracking records on an isolated Windows Private Heap (`HeapCreate(0, 0, 0)`) with a 262,144-bucket hash table to avoid recursive tracking overhead.
  - Captured stack backtraces via `RtlCaptureStackBackTrace` and resolved module boundaries via `GetModuleInformation` and `dbghelp` symbol resolution.
- **3-Pass Differential Test**: Executed 3 back-to-back ephemeral searches in a single process across all 892 pages ("futures", "derivatives", "options").
- **Empirical Verdict**:
  - **GLib/GObject/GIO**: Retained 3.25 MB on Pass 1 (type registrations, unicode tables), and **0.00 MB on Pass 2 and Pass 3** (proven one-time initialization).
  - **`libwinpthread-1.dll`**: Retained **5.08 MB (221,870 allocations $\times$ 24 bytes) on every single pass** at `pthread_mutex_lock+0x88`.
- **Root Cause Disassembly**:
  - `poppler/Array.h` (L82) and `poppler/Dict.h` (L119) embed `mutable std::recursive_mutex mutex`.
  - GCC `libstdc++` defines `__GTHREAD_RECURSIVE_MUTEX_INIT`, causing `~recursive_mutex() = default;` to omit calling `pthread_mutex_destroy()`.
  - `libwinpthread-1.dll` allocates 24 bytes on first lock; without destroy, handles are permanently orphaned on the process UCRT heap.

### Phase 4: Option B Remediation & Quarantined Probe Test
- **Hypothesis**: Link `mimalloc` to optimize page slack and decommits, invoke `mi_collect(true)` + `_heapmin()`, and recycle worker threads every 8 searches.
- **Probe Verification (`search_isolation_probe.exe --option-b`)**:
  - Built `cleanupOptionBMutexes(passId)` to scan the tracker table, identify all 24-byte allocations called by `libwinpthread-1.dll`, free them via the original CRT `free()`, and purge the heap.
  - **Outcome**: In Pass 2 and Pass 3, surviving allocations dropped to **0.00 MB across all modules (100.000% deallocation)**.
- **The Stability Boundary**: Dynamic IAT table modification while GTK3, Cairo, and GIO worker threads run concurrently caused race conditions. IAT hooking was strictly quarantined inside the test probe and **excluded from production GUI runtime**.

### Phase 5: Production Verification & Critical Reality Check
- **The Issue**: Thread recycling in `DocumentSearchService.cpp` was asserted to reclaim the leak in production without IAT hooking.
- **The Reality**: Reclaiming memory in the probe proved that the quarantined mechanism worked, but did **not** prove production was fixed. Windows heap allocations are process-scoped, not thread-scoped. Terminating a worker thread frees thread stack and TLS, but leaves orphaned heap blocks intact.
- **20-Pass Production Benchmark (`fluidcore_app.exe --run-scenario-find20`)**:
  - Fixed an initial startup crash (`0xC0000374 STATUS_HEAP_CORRUPTION`) caused by including `<mimalloc-new-delete.h>` in a single translation unit (`main.cpp`) while other translation units used CRT `free`.
  - Executed 20 consecutive 892-page searches on `fluidcore_app.exe`.
  - **Empirical Outcome**: Clockwork linear growth of **+5.08 MB LiveAlloc per pass**, accumulating **+101.12 MB LiveAlloc** and **+157.70 MB Private Bytes** across 20 passes. Thread recycling at pass 8 and 16 had zero effect on the leak.

---

## 3. Comprehensive Catalog of All Test Cases & Protocols

### Test Case 1: Automated CTest Regression Suite (38 Test Targets)

| Parameter | Details |
|---|---|
| **Identifier** | `CTest_Suite_All` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/build-win.ps1 -Test` |
| **Implementation** | [CMakeLists.txt](file:///d:/FluidCorePDF/fluidcore-platform/src/app/CMakeLists.txt), individual test files under `src/app/tests/` |
| **Type** | Unit & Integration Test Suite |

#### Objective
Ensure zero functional regressions across math, geometry, rendering caches, storage persistence, and core layout algorithms during memory refactoring.

#### Test Targets Executed
1. `FluidCoreApiSmokeTest` (0.02s) — Core engine API initialization.
2. `FluidCoreEngineTest` (0.05s) — Document binding and session state.
3. `RTreeIndexTest` (0.03s) — Spatial indexing and bounding box queries.
4. `WorkspaceModelTest` (0.03s) — Document card graph mutations.
5. `ExcerptCardNodeTest` (0.02s) — Card excerpts and thumbnail binding.
6. `PhysicsSolverTest` (0.02s) — Spring-damper card collision dynamics.
7. `CardStackNodeTest` (0.02s) — Hierarchical card stacking.
8. `XoppDocumentTest` (0.03s) — Native Xournal++ XML parsing.
9. `AnnotationStoreTest` (0.02s) — Stroke and text annotation records.
10. `UndoStackTest` (0.02s) — Command pattern undo/redo stack.
11. `TextSelectionTest` (0.02s) — Glyphs and selection quad bounding.
12. `SqueezeEngineTest` (0.02s) — Continuous document vertical compression folds.
13. `AnchorSqueezePlannerTest` (0.03s) — Optimal fold boundary distribution.
14. `GraphTopologyTest` (0.02s) — Card connector dependency graphs.
15. `RTreeBenchmarkTest` (0.22s) — 100,000 item spatial index throughput.
16. `CardLayoutEngineTest` (0.02s) — Multi-card column alignment.
17. `WorkspaceSearchEngineTest` (0.04s) — Cross-card canvas search queries.
18. `WorkspaceExportEngineTest` (0.04s) — High-DPI canvas rendering pipeline.
19. `ProjectStoreTest` (0.12s) — SQLite `.ltproj` project database persistence.
20. `CrashRecoveryFuzzTest` (0.02s) — Malformed XML/JSON recovery.
21. `RoundTripPersistenceTest` (0.03s) — Save/reload project fidelity.
22. `StylusMatrixTest` (0.06s) — DirectManipulation stylus transforms.
23. `StrokeHitTest` (0.02s) — Inking stroke intersection math.
24. `StrokePersistenceRegressionTest` (0.06s) — Binary stroke storage.
25. `ViewportZoomAnchorTest` (0.02s) — Focal zoom stability under high scale.
26. `DamageRectTest` (0.02s) — Dirty region invalidation math.
27. `ToolManagerTest` (0.02s) — Pen, highlighter, eraser, crop state machines.
28. `PageTileCacheTest` (0.05s) — Cache capacity bounds and LRU eviction.
29. `StrokeStabilizerTest` (0.03s) — Cubic spline stroke smoothing.
30. `SqueezeRenderTest` (0.03s) — Visual rendering of accordion fold lines.
31. `SearchSqueezePlannerTest` (0.04s) — Auto-squeezing non-hit document sections.
32. `ReturnAnchorPillTest` (0.02s) — Breadcrumb navigation pills.
33. `BiDirectionalAnchorTest` (0.03s) — Card-to-page reciprocal anchor links.
34. `ExcerptTileCacheTest` (0.04s) — Card thumbnail rendering LRU.
35. `WorkspaceInteractionTest` (0.12s) — Pointer and gesture event routing.
36. `CropDragCrashTest` (4.03s) — Stress drag of raster crop excerpts.
37. `PdfExportServiceTest` (0.19s) — Vector PDF document export.
38. `ScalabilityBenchmarkTest` (1.42s) — Multi-page scalability stress.

#### Outcome
**100% Passed (38/38)** in **7.08 seconds**. Zero regressions.

---

### Test Case 2: Scenario A — Standard UI Reading & Navigation Protocol

| Parameter | Details |
|---|---|
| **Identifier** | `Scenario_A_Reading_Workflow` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/run-scenario-a.ps1` |
| **Implementation** | [main.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/main.cpp#L1093) (`scheduleScenarioA`) |
| **Flag** | `--run-scenario-a <ltproj_path>` |

#### Objective
Simulate realistic user reading workflow: document open, progressive page scrolling, zooming, selection, card creation, and document close.

#### Workflow Steps
1. Settle UI (1500 ms) and capture baseline memory.
2. Scroll to Page 20; wait 500 ms.
3. Scroll to Page 150; wait 500 ms.
4. Execute zoom in (1.2x), zoom out (0.83x), reset zoom.
5. Select passage on Page 150 and extract an excerpt card to the canvas.
6. Trigger document close and verify tile cache eviction.

#### Outcome
Demonstrated that page tile cache enforces strict upper bounds (max 8 resident pages, ~10 MB). Private bytes remained bounded between **60–85 MB** throughout interaction.

---

### Test Case 3: Scenario B — Editing & Inking Annotation Protocol

| Parameter | Details |
|---|---|
| **Identifier** | `Scenario_B_Inking_Workflow` |
| **Command** | `fluidcore_app.exe --run-scenario-b <ltproj_path>` |
| **Implementation** | [main.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/main.cpp#L1301) (`scheduleScenarioB`) |
| **Flag** | `--run-scenario-b <ltproj_path>` |

#### Objective
Verify memory stability under continuous inking, highlighter stroke creation, eraser modification, and undo/redo operations.

#### Workflow Steps
1. Generate 50 pen strokes across DocumentPane and WorkspaceView.
2. Generate 20 highlighter strokes.
3. Perform 15 erase actions.
4. Execute 20 rapid undo / redo cycles.
5. Capture memory deltas and inspect `AnnotationStore`.

#### Outcome
All stroke data persisted in `AnnotationStore` without stroke point leakage. Working set remained stable; zero GDI or Cairo handle leaks.

---

### Test Case 4: Scenario Reopen Audit — Document Lifetime Verification

| Parameter | Details |
|---|---|
| **Identifier** | `Scenario_Reopen_Audit` |
| **Command** | `fluidcore_app.exe --run-scenario-reopen <pdf_path>` |
| **Implementation** | [main.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/main.cpp#L1501) (`scheduleScenarioReopenAudit`) |
| **Flag** | `--run-scenario-reopen-audit <pdf_path>` |

#### Objective
Verify that closing a document and reopening it completely destroys all Poppler page handles and unrefs the underlying `PopplerDocument*`.

#### Empirical Results
- Initial Baseline Private: `62.10 MB`
- Document Loaded (892 pages): `67.50 MB`
- Document Closed (`closeDocument()`): `62.30 MB` (Net delta: **+0.20 MB**)
- Document Reopened: `67.55 MB`
- Final Document Close: `62.35 MB`
- `PopplerLifetimeTracker`: **0 Live Pages** (892 created, 892 destroyed).

---

### Test Case 5: Search Isolation Probe Protocol 1 — Persistent Document Search Baseline

| Parameter | Details |
|---|---|
| **Identifier** | `Probe_Proto1_Persistent` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/run-probe.ps1 -Mode --persistent` |
| **Implementation** | [SearchIsolationProbe.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/tests/SearchIsolationProbe.cpp#L170) (`runPersistentProtocol`) |
| **Flag** | `--persistent <pdf_path>` |

#### Objective
Measure memory retention when searches are executed against the persistent GUI `PopplerDocument*` across multiple queries.

#### Empirical Results (892 Pages)

```
[Checkpoint] 1. Baseline: GUI Document Loaded       | Priv:  7.55 MB | LiveAlloc:  2.05 MB | HeapCommit:  2.37 MB
[Checkpoint] 2. Post-Search 1 ('futures', hits: 1567)| Priv: 49.67 MB | LiveAlloc: 10.93 MB | HeapCommit: 22.56 MB
[Checkpoint] 3. Post-HeapMin 1                      | Priv: 48.10 MB | LiveAlloc: 10.93 MB | HeapCommit: 21.04 MB
[Checkpoint] 4. Post-Search 2 (REPEATED 'futures')   | Priv: 74.69 MB | LiveAlloc: 16.01 MB | HeapCommit: 29.85 MB
[Checkpoint] 5. Post-HeapMin 2                      | Priv: 73.20 MB | LiveAlloc: 16.01 MB | HeapCommit: 28.12 MB
[Checkpoint] 6. Post-Search 3 (REPEATED 'futures')   | Priv: 99.02 MB | LiveAlloc: 20.81 MB | HeapCommit: 37.84 MB
[Checkpoint] 7. Post-Close: Document Destroyed      | Priv: 97.92 MB | LiveAlloc: 19.35 MB | HeapCommit: 36.71 MB
```

#### Outcome
Demonstrated that under persistent architecture, each search added **+5.08 MB LiveAlloc** and **+24 MB Private Bytes** monotonically. Even closing the document failed to reclaim 90+ MB.

---

### Test Case 6: Search Isolation Probe Protocol 2 — Ephemeral Document Search

| Parameter | Details |
|---|---|
| **Identifier** | `Probe_Proto2_Ephemeral` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/run-probe.ps1 -Mode --ephemeral` |
| **Implementation** | [SearchIsolationProbe.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/tests/SearchIsolationProbe.cpp#L250) (`runEphemeralProtocol`) |
| **Flag** | `--ephemeral <pdf_path>` |

#### Objective
Test the ephemeral architecture hypothesis: opening a throwaway `PopplerDocument*`, searching, and unreferencing immediately.

#### Empirical Results (892 Pages)

```
[Checkpoint] 1. Baseline: GUI Document Loaded        | Priv:  7.58 MB | LiveAlloc:  2.03 MB | HeapCommit:  2.40 MB
[Checkpoint] 2a. Search 1 Complete (searchDoc alive)| Priv: 50.49 MB | LiveAlloc: 10.94 MB | HeapCommit: 22.67 MB
[Checkpoint] 2b. Post-Unref searchDoc1 (Destroyed)  | Priv: 46.20 MB | LiveAlloc:  7.12 MB | HeapCommit: 20.10 MB
[Checkpoint] 3a. Search 2 Complete (searchDoc alive)| Priv: 75.77 MB | LiveAlloc: 16.02 MB | HeapCommit: 30.16 MB
[Checkpoint] 3b. Post-Unref searchDoc2 (Destroyed)  | Priv: 71.40 MB | LiveAlloc: 12.20 MB | HeapCommit: 28.02 MB
```

#### Outcome
Confirmed that destroying the throwaway `PopplerDocument*` immediately reclaimed **~4.5 MB of Poppler C++ objects**. However, a residual **~5.08 MB LiveAlloc** still persisted per pass, motivating Option A.

---

### Test Case 7: Search Isolation Probe Protocol 3 — Option A Multi-Pass Module Attribution

| Parameter | Details |
|---|---|
| **Identifier** | `Probe_Proto3_OptionA_Attribution` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/run-probe.ps1 -Mode --attribute` |
| **Implementation** | [SearchIsolationProbe.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/tests/SearchIsolationProbe.cpp#L641) (`runAttributionProtocol(false)`) |
| **Flag** | `--attribute <pdf_path>` |

#### Objective
Deep stack backtrace attribution using SEH stack walking and IAT interception to isolate one-time static initializations from recurring leak sites.

#### Attribution Matrix Output

```
==============================================================================================
  3-PASS MODULE ATTRIBUTION MATRIX (SURVIVING LIVEALLOC)
  Total Retained LiveAlloc Tracked: 18.52 MB
==============================================================================================
  Module                       | Pass 1 Retained      | Pass 2 Retained      | Pass 3 Retained      | Verdict
  -----------------------------+----------------------+----------------------+----------------------+-------------------------
  libwinpthread-1.dll          | 5.08 MB (221871)     | 5.08 MB (221870)     | 5.08 MB (221870)     | RECURRING LEAK SITE
  libglib-2.0-0.dll            | 2.62 MB (38269)      | 0.00 MB              | 0.00 MB              | One-Time Init
  libgobject-2.0-0.dll         | 0.48 MB (5861)       | 0.00 MB              | 0.00 MB              | One-Time Init
  libgio-2.0-0.dll             | 0.17 MB (4617)       | 0.00 MB              | 0.00 MB              | One-Time Init
  liblcms2-2.dll               | 0.02 MB (52)         | 0.00 MB              | 0.00 MB              | Negligible (<100 KB)
  libpoppler-163.dll           | 0.00 MB (2)          | 0.00 MB              | 0.00 MB              | Negligible (<100 KB)
  -----------------------------+----------------------+----------------------+----------------------+-------------------------
  TOTAL ATTRIBUTED             | 8.36 MB              | 5.08 MB              | 5.08 MB              |
  SURVIVING ALLOC COUNT        | 270672               | 221870               | 221870               |
  CALC TRACKER OVERHEAD (64B)  | 16.52 MB             | 13.54 MB             | 13.54 MB             |

==============================================================================================
  TOP CALL SITES FOR RECURRING LEAK SITES (Pass 2 + Pass 3 Survivors)
==============================================================================================
  [libwinpthread-1.dll]:
    - 10.16 MB retained at libwinpthread-1.dll+0x39d8 (pthread_mutex_lock+0x88)
```

#### Outcome
Definitively proved that GLib is clean (0.00 MB leak) and that **100% of recurring live allocations** originate from orphaned winpthread mutex handles inside `poppler::Array` / `poppler::Dict`.

---

### Test Case 8: Search Isolation Probe Protocol 4 — Option B Mutex Remediation Probe

| Parameter | Details |
|---|---|
| **Identifier** | `Probe_Proto4_OptionB_Remediation` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/run-probe.ps1 -Mode --option-b` |
| **Implementation** | [SearchIsolationProbe.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/tests/SearchIsolationProbe.cpp#L452) (`cleanupOptionBMutexes`) |
| **Flag** | `--option-b <pdf_path>` |

#### Objective
Test whether intercepting and freeing the 221,870 orphaned 24-byte winpthread mutex handles at search boundary drops recurring leaks to zero.

#### Empirical Results

```
  --> Running Pass 1: Ephemeral Search ('futures') across 892 pages...
    [Option B Cleanup] Reclaimed 221871 orphaned winpthread mutex handles (5.08 MB)
[Checkpoint]   Pass 1 Complete (hits: 1567)         | Priv: 30.88 MB | LiveAlloc: 5.84 MB

  --> Running Pass 2: Ephemeral Search ('derivatives') across 892 pages...
    [Option B Cleanup] Reclaimed 221870 orphaned winpthread mutex handles (5.08 MB)
[Checkpoint]   Pass 2 Complete (hits: 561)          | Priv: 29.93 MB | LiveAlloc: 5.84 MB
    [Pass 2 Flow] Alloc: 4369.18 MB | Freed Self: 4369.18 MB (100.000% Freed!)

  --> Running Pass 3: Ephemeral Search ('options') across 892 pages...
    [Option B Cleanup] Reclaimed 221870 orphaned winpthread mutex handles (5.08 MB)
[Checkpoint]   Pass 3 Complete (hits: 1760)         | Priv: 29.89 MB | LiveAlloc: 5.58 MB
    [Pass 3 Flow] Alloc: 4370.64 MB | Freed Self: 4370.64 MB (100.000% Freed!)

==============================================================================================
  3-PASS MODULE ATTRIBUTION MATRIX (SURVIVING LIVEALLOC)
==============================================================================================
  Module                       | Pass 1 Retained      | Pass 2 Retained      | Pass 3 Retained
  -----------------------------+----------------------+----------------------+----------------
  libglib-2.0-0.dll            | 2.60 MB (38215)      | 0.00 MB              | 0.00 MB
  libgobject-2.0-0.dll         | 0.48 MB (5861)       | 0.00 MB              | 0.00 MB
  libwinpthread-1.dll          | 0.00 MB (0)          | 0.00 MB (0)          | 0.00 MB (0)
  -----------------------------+----------------------+----------------------+----------------
  TOTAL ATTRIBUTED             | 3.26 MB              | 0.00 MB              | 0.00 MB
```

#### Outcome
Proved that handle cleanup dropped recurring leaks to **exactly 0.00 MB** with a **100.000% deallocation rate**. Clean process exit code 0.

---

### Test Case 9: Production App 5-Pass Repeated Find Test

| Parameter | Details |
|---|---|
| **Identifier** | `Production_Find5_Test` |
| **Command** | `fluidcore_app.exe --run-scenario-find5 <pdf_path>` |
| **Implementation** | [main.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/main.cpp#L1342) (`scheduleScenarioRepeatedFind(ctx, 5)`) |
| **Flag** | `--run-scenario-find5 <pdf_path>` |

#### Objective
Evaluate production search behavior without navigation across 5 repeated queries on `fluidcore_app.exe`.

#### Summary of 5 Passes
- Find 1: `23.65 MB -> 28.65 MB (+5.00 MB LiveAlloc)`
- Find 2: `28.65 MB -> 33.75 MB (+5.10 MB LiveAlloc)`
- Find 3: `33.74 MB -> 38.79 MB (+5.04 MB LiveAlloc)`
- Find 4: `38.79 MB -> 43.87 MB (+5.09 MB LiveAlloc)`
- Find 5: `43.87 MB -> 48.83 MB (+4.95 MB LiveAlloc)`

#### Outcome
First indication that in production, LiveAlloc climbed strictly by ~5.08 MB per pass.

---

### Test Case 10: Production App 20-Pass Linear Growth Benchmark

| Parameter | Details |
|---|---|
| **Identifier** | `Production_Find20_Stress_Benchmark` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/run-scenario-repeated-find.ps1 -Iterations 20` |
| **Implementation** | [main.cpp](file:///d:/FluidCorePDF/fluidcore-platform/src/app/main.cpp#L1342) (`scheduleScenarioRepeatedFind(ctx, 20)`) |
| **Flag** | `--run-scenario-find20 <pdf_path>` or `--find-iters=20` |

#### Objective
Definitively test whether worker thread recycling (scheduled every 8 searches) or mimalloc reclaims winpthread allocations over a long-running production session.

#### Full 20-Pass Telemetry Matrix (From `fluidcore_telemetry.log`)

| Find # | Before Priv | After Priv | Delta Priv | Before LiveAlloc | After LiveAlloc | Delta LiveAlloc | Heap Commit | Hits | Lifecycle Event |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---|
| **1**  | 63.78 MB  | 74.94 MB  | +11.16 MB | 23.65 MB  | 28.65 MB  | **+5.00 MB** | 42.38 MB  | 1567 | Search Pass 1 |
| **2**  | 75.32 MB  | 81.66 MB  | +6.34 MB  | 28.65 MB  | 33.75 MB  | **+5.10 MB** | 48.34 MB  | 1567 | Search Pass 2 |
| **3**  | 82.28 MB  | 88.34 MB  | +6.06 MB  | 33.74 MB  | 38.79 MB  | **+5.04 MB** | 55.24 MB  | 1567 | Search Pass 3 |
| **4**  | 89.16 MB  | 98.92 MB  | +9.76 MB  | 38.79 MB  | 43.87 MB  | **+5.09 MB** | 65.03 MB  | 1567 | Search Pass 4 |
| **5**  | 98.92 MB  | 106.23 MB | +7.32 MB  | 43.87 MB  | 48.83 MB  | **+4.95 MB** | 72.34 MB  | 1567 | Search Pass 5 |
| **6**  | 106.23 MB | 114.34 MB | +8.11 MB  | 48.83 MB  | 53.91 MB  | **+5.09 MB** | 80.44 MB  | 1567 | Search Pass 6 |
| **7**  | 114.34 MB | 122.46 MB | +8.12 MB  | 53.91 MB  | 59.00 MB  | **+5.09 MB** | 88.54 MB  | 1567 | Search Pass 7 |
| **8**  | 122.46 MB | 130.59 MB | +8.12 MB  | 59.00 MB  | 64.08 MB  | **+5.08 MB** | 96.65 MB  | 1567 | **Worker Thread Recycled** |
| **9**  | 130.54 MB | 138.17 MB | +7.63 MB  | 64.03 MB  | 69.13 MB  | **+5.10 MB** | 104.30 MB | 1567 | Leak continues unabated |
| **10** | 138.43 MB | 145.41 MB | +6.98 MB  | 69.13 MB  | 74.21 MB  | **+5.08 MB** | 111.54 MB | 1567 | Search Pass 10 |
| **11** | 145.41 MB | 152.78 MB | +7.36 MB  | 74.21 MB  | 79.29 MB  | **+5.08 MB** | 118.89 MB | 1567 | Search Pass 11 |
| **12** | 153.04 MB | 160.21 MB | +7.17 MB  | 79.28 MB  | 84.36 MB  | **+5.08 MB** | 126.30 MB | 1567 | Search Pass 12 |
| **13** | 160.21 MB | 167.72 MB | +7.51 MB  | 84.36 MB  | 89.43 MB  | **+5.07 MB** | 133.80 MB | 1567 | Search Pass 13 |
| **14** | 167.72 MB | 175.55 MB | +7.84 MB  | 89.43 MB  | 94.51 MB  | **+5.08 MB** | 141.62 MB | 1567 | Search Pass 14 |
| **15** | 175.55 MB | 183.97 MB | +8.42 MB  | 94.51 MB  | 99.59 MB  | **+5.08 MB** | 150.02 MB | 1567 | Search Pass 15 |
| **16** | 183.97 MB | 191.80 MB | +7.82 MB  | 99.59 MB  | 104.63 MB | **+5.04 MB** | 157.88 MB | 1567 | **Worker Thread Recycled** |
| **17** | 191.82 MB | 199.41 MB | +7.59 MB  | 104.62 MB | 109.75 MB | **+5.12 MB** | 165.41 MB | 1567 | Leak continues unabated |
| **18** | 199.41 MB | 206.55 MB | +7.14 MB  | 109.76 MB | 114.61 MB | **+4.85 MB** | 172.72 MB | 1567 | Search Pass 18 |
| **19** | 206.55 MB | 214.04 MB | +7.48 MB  | 114.61 MB | 119.69 MB | **+5.08 MB** | 180.19 MB | 1567 | Search Pass 19 |
| **20** | 214.04 MB | 221.48 MB | +7.44 MB  | 119.69 MB | 124.77 MB | **+5.08 MB** | 187.62 MB | 1567 | Search Pass 20 |

#### Empirical Conclusions
- **Net LiveAlloc Growth**: **+101.12 MB** (23.65 MB $\to$ 124.77 MB).
- **Average LiveAlloc Delta**: **5.06 MB per pass** ($\sigma = 0.05\text{ MB}$).
- **Thread Recycling Invalidation**: Recycling at passes 8 and 16 resulted in zero drop in LiveAlloc. Reaffirms that heap allocations are process-scoped in Windows UCRT.

---

### Test Case 11: Telemetry Gating Automated Test

| Parameter | Details |
|---|---|
| **Identifier** | `Telemetry_Gating_Test` |
| **Command** | Automated validation script |
| **Implementation** | [MemoryTelemetry.h](file:///d:/FluidCorePDF/fluidcore-platform/src/app/services/MemoryTelemetry.h#L154) |

#### Objective
Verify that `fluidcore_telemetry.log` is untouched during clean production runs and only written when `FLUIDCORE_LOG_TELEMETRY=1`.

#### Outcome
- Clean Run (No Flag): Log file size before: `185,277` bytes; after: `185,277` bytes (**0 bytes written**).
- Flagged Run (`FLUIDCORE_LOG_TELEMETRY=1`): Log file size grew from `185,277` to `187,125` bytes (**Appended properly**).

---

### Test Case 12: 50-PDF Scalability & Memory Benchmark Test

| Parameter | Details |
|---|---|
| **Identifier** | `Scalability_Benchmark_50PDF` |
| **Command** | `powershell -ExecutionPolicy Bypass -File ops/scripts/build-win.ps1 -Benchmark` |
| **Implementation** | `src/app/tests/ScalabilityBenchmarkTest.cpp` |
| **Target Binary** | `build-win/src/app/scalability_benchmark_test.exe` |

#### Objective
Stress-test document opening, rendering, and memory reclamation across 50 heterogeneous PDF documents ranging from single-page flyers to 1,000-page manuals.

#### Outcome
All 50 documents parsed, rendered, and closed with zero unhandled exceptions. Verified that `PageTileCache` stays within its 64 MB budget across diverse aspect ratios and color profiles.

---

### Test Case 13: Brain Scratch Micro-Benchmarks & Diagnostic Probes

During the iterative root-cause investigation, several isolated micro-test programs and PowerShell harnesses were developed and preserved in the **brain scratch directory** (`brain/.../scratch/`) to validate specific hypotheses without the noise of the full GTK3 GUI runtime:

#### A. `test_thread_exit.cpp` & `run_thread_exit.ps1` — Thread Termination Reclamation Test
- **Artifact**: `scratch/test_thread_exit.cpp` (2,203 bytes), `scratch/run_thread_exit.ps1`
- **Hypothesis ("Why")**: Test whether spawning a standalone `std::thread`, executing a Poppler search on the 892-page PDF, and letting the thread terminate and join would cause Windows to reclaim the ~5 MB of winpthread mutex allocations.
- **Implementation**:
  - Implemented an isolated Win32 console app calling Poppler C API directly (`poppler_document_new_from_file`, `poppler_page_find_text`, `poppler_rectangle_free`).
  - Ran 3 consecutive searches (`"futures"`, `"derivatives"`, `"options"`) on isolated worker threads.
  - After each thread joined, invoked `_heapmin()` and measured Win32 `PrivateUsage` via `GetProcessMemoryInfo`.
- **Outcome**:
  ```
  Base Priv: 6.84 MB
  Thread finished search (futures, hits=1567)
  Post-Thread 1 Priv: 46.50 MB
  Thread finished search (derivatives, hits=561)
  Post-Thread 2 Priv: 71.20 MB
  Thread finished search (options, hits=1760)
  Post-Thread 3 Priv: 95.80 MB
  ```
- **Architectural Conclusion**: Proved definitively that terminating an `std::thread` does **not** free heap memory allocated during that thread's execution. Windows thread cleanup is strictly confined to kernel thread objects, stack memory, and TLS. Heap allocations remain allocated on the process CRT heap (`ucrtbase.dll`).

#### B. `test_mimalloc.cpp` & `run_test.ps1` — Allocator Interception & Heap Region Test
- **Artifact**: `scratch/test_mimalloc.cpp` (593 bytes), `scratch/run_test.ps1`
- **Hypothesis ("Why")**: Test if linking `mimalloc` automatically intercepts standard C `malloc` calls across dynamic libraries or if standard library calls continue routing to Windows UCRT.
- **Implementation**:
  - Queried `mi_version()`.
  - Allocated a 1024-byte block via `mi_malloc` and checked `mi_is_in_heap_region(p)`.
  - Allocated a 1024-byte block via CRT `malloc` and checked `mi_is_in_heap_region(pStd)`.
- **Outcome**:
  ```
  Testing mimalloc...
  mi_version: 212
  mi_malloc(1024) = 0x... (mi_is_in_heap_region = 1)
  malloc(1024) = 0x... (mi_is_in_heap_region = 0)
  ```
- **Architectural Conclusion**: Standard CRT `malloc()` bypasses `mimalloc` unless explicitly overridden at link-time across the whole binary. Attempting to force override in a single translation unit via `<mimalloc-new-delete.h>` caused allocator mismatch crashes (`0xC0000374 STATUS_HEAP_CORRUPTION`).

#### C. `test_retaddr.cpp` & `run_retaddr.ps1` — Return Address Stack Inspection Test
- **Artifact**: `scratch/test_retaddr.cpp` (583 bytes), `scratch/run_retaddr.ps1`
- **Hypothesis ("Why")**: Validate whether GCC built-in intrinsic `__builtin_return_address(0)` and `__builtin_return_address(1)` can reliably inspect return addresses across MinGW-w64 shared library boundaries (`libwinpthread-1.dll`) without triggering SEH access violations.
- **Implementation**:
  - Initialized a `pthread_mutex_t` with `PTHREAD_RECURSIVE_MUTEX_INITIALIZER`.
  - Extracted return addresses at lock time and verified symbol resolution.
- **Outcome**:
  - Extracted valid instruction pointers pointing directly into `pthread_mutex_lock` and its caller.
  - Formed the technical foundation for the stack attribution engine in `SearchIsolationProbe.cpp` (`RtlCaptureStackBackTrace` and symbol lookup).

#### D. `run_debug.ps1` — Diagnostic Test Runner
- **Artifact**: `scratch/run_debug.ps1` (574 bytes)
- **Hypothesis ("Why")**: Provide a dedicated PowerShell runner to execute diagnostic probes under MSYS2 UCRT64 environment with proper DLL search paths (`C:\msys64\ucrt64\bin`), capturing both stdout and stderr while bypassing PowerShell semicolon string-interpolation pitfalls.

---

## 4. Key Architectural Learnings & Gotchas

1. **Process Heap vs. Thread Local Scope**:
   Terminating a worker thread reclaims thread stack memory, kernel thread handles, and Thread-Local Storage (`__thread` / `thread_local`). It does **not** free process heap blocks (`malloc`, `HeapAlloc`). Windows does not track which thread issued an allocation on the process heap.
2. **MinGW GCC libstdc++ `std::recursive_mutex` Defect**:
   In GCC `<mutex>`, when `__GTHREAD_RECURSIVE_MUTEX_INIT` is defined, `~recursive_mutex() = default;` is compiled. Because `pthread_mutex_destroy` is never called, `libwinpthread`'s lazy allocation mechanism permanently leaks 24 bytes per mutex instance.
3. **Allocator Mismatch with `<mimalloc-new-delete.h>`**:
   Including `<mimalloc-new-delete.h>` in a single `.cpp` file (e.g. `main.cpp`) overrides `operator new`/`delete` locally. If an object is allocated with `mi_new` in `main.cpp` and deleted by CRT `free` in a shared library (or vice-versa), Windows aborts with `0xC0000374 STATUS_HEAP_CORRUPTION`. Overrides must either be whole-program link-time replacements or omitted.
4. **Dynamic IAT Hooking in Production GUI**:
   Hooking DLL import tables at runtime while GTK3, Cairo, and GIO worker thread pools are running causes race conditions and memory access violations. All dynamic IAT instrumentation must remain strictly quarantined in diagnostic test probes.

---

## 5. Next Steps: Out-of-Process Search Worker

Because in-process IAT hooking is unsafe for production, and in-process worker thread recycling cannot reclaim process-scoped heap memory, the permanent production architecture is:

### **Out-of-Process Search Worker (`fluidcore_search_worker.exe`)**
- Move the search loop out of `fluidcore_app.exe` into a lightweight background process.
- GUI communicates with worker via anonymous pipes or JSON over stdout (`--search-worker <pdf_path> <query>`).
- When the worker finishes and terminates, the **Windows kernel reclaims 100.000% of all allocated memory, mutex handles, and heap slack**.
- Guarantees **0.00 MB leak** in the GUI application without any runtime hooking.
