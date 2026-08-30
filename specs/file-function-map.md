# Xournal++ & FluidCore File & Function Architecture Mapping

This document provides a comprehensive map of the **Xournal++** codebase augmented by the **Decoupled C++ Core Engine (`libfluidcore`)**, detailing how source files, headers, and classes are structured, what responsibilities each component handles, and how they interconnect across subsystems.

---

## 1. Subsystem Interconnection Matrix

```mermaid
graph TD
    subgraph Execution Entry
        EXE["src/exe/Xournalpp.cpp"] --> XMAIN["src/core/control/XournalMain.cpp"]
    end

    subgraph Central Coordination
        XMAIN --> CTRL["src/core/control/Control.cpp"]
        CTRL --> TOOL_MGR["src/core/control/ToolHandler.cpp"]
        CTRL --> SCHED["src/core/control/jobs/XournalScheduler.cpp"]
        CTRL --> SQUEEZE_CTRL["src/core/control/SqueezeController.cpp"]
        CTRL --> UNDO_MGR["src/core/undo/UndoRedoHandler.cpp"]
    end

    subgraph Decoupled Core Engine (libfluidcore / C++20)
        CTRL --> FC_API["libfluidcore/FluidCoreAPI.h"]
        FC_API --> WS_MODEL["libfluidcore/workspace/WorkspaceModel.cpp"]
        FC_API --> SQ_ENG["libfluidcore/squeeze/SqueezeEngine.cpp"]
        FC_API --> GRAPH_ENG["libfluidcore/graph/GraphTopology.cpp"]
        FC_API --> PROJ_STORE["libfluidcore/storage/ProjectStore.cpp"]
        WS_MODEL --> RTREE["libfluidcore/workspace/RTreeIndex.h"]
        PROJ_STORE --> SQLITE_WAL["libfluidcore/storage/SqliteWalDb.cpp"]
    end

    subgraph GUI & Dual-Pane Viewport (GTK 3 / Cairo)
        CTRL --> MAIN_WIN["src/core/gui/MainWindow.cpp (GtkPaned)"]
        MAIN_WIN --> X_VIEW["src/core/gui/XournalView.cpp (Left Pane)"]
        MAIN_WIN --> WS_VIEW["src/core/gui/workspace/WorkspaceView.cpp (Right Pane)"]
        X_VIEW --> PAGE_VIEW["src/core/gui/PageView.cpp"]
        X_VIEW --> RETURN_PILL["src/core/gui/widgets/ReturnAnchorPill.cpp"]
    end

    subgraph Input Processing & Gestures
        MAIN_WIN --> IN_CTX["src/core/gui/inputdevices/InputContext.cpp"]
        IN_CTX --> PEN_IN["src/core/gui/inputdevices/PenInputHandler.cpp"]
        IN_CTX --> TOUCH_IN["src/core/gui/inputdevices/TouchInputHandler.cpp"]
        IN_CTX --> DESK_IN["src/core/gui/inputdevices/DesktopSqueezeHandler.cpp"]
        IN_CTX --> HAND_REC["src/core/gui/inputdevices/HandRecognition.cpp"]
    end

    subgraph In-Memory Document Model
        CTRL --> DOC["src/core/model/Document.cpp"]
        DOC --> PAGE["src/core/model/XojPage.cpp"]
        PAGE --> LAYER["src/core/model/Layer.cpp"]
        LAYER --> ELEMS["src/core/model/Stroke.cpp / Text.cpp / Image.cpp"]
    end

    subgraph Persistence & I/O
        CTRL --> LT_LOADER["src/core/control/ltproj/LtProjLoader.cpp"]
        CTRL --> LT_SAVER["src/core/control/ltproj/LtProjSaver.cpp"]
        LT_LOADER --> PROJ_STORE
        LT_SAVER --> PROJ_STORE
    end
```

---

## 2. Granular Module Breakdown

### 2.1 Decoupled Core Engine (`libfluidcore/`)

> M0 status: `FluidCoreAPI.h` (TRD §4.1 signatures, persistence stub-only per ADR-0002) and
> module stubs for `SqueezeEngine`, `WorkspaceModel`/`RTreeIndex`, `GraphTopology`,
> `ProjectStore` exist and compile; headless smoke test passes via ctest.
> Wave-1 slice: `FluidCoreEngine` implements the spatial scene-graph methods
> (`insertNode`, `updateNodePosition`, `removeNode`, `queryVisibleNodes`, geometry
> getters) over a working `WorkspaceModel` + dynamic R-tree `RTreeIndex`; squeeze,
> edge routing, and persistence remain milestone-stubbed (M2/M4/M5).

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| `libfluidcore/FluidCoreAPI.h` | `class FluidCoreAPI` | Pure abstract C++ interface exposing document geometry registration, coordinate transforms, UUID-based node registry, spatial range queries, graph edges, and pure geometry getters (node bounds, positions, spline control points) to the GUI frontend. No Cairo/GTK types cross this boundary — all rendering lives in the GTK layer. |
| `libfluidcore/FluidCoreEngine.cpp` | `class FluidCoreEngine : FluidCoreAPI` | Concrete facade consumed by the GUI. Delegates the live spatial slice to `WorkspaceModel`; squeeze/graph/persistence delegate points are no-op stubs until M2/M4/M5. |
| `libfluidcore/workspace/WorkspaceModel.cpp` | `class WorkspaceModel` | Spatial scene graph managing polymorphic `WorkspaceNode` entities keyed by their own UUID ids, tracking authoritative node positions/sizes and viewport queries via the index. Thread safety, stack-depth validation, and dirty-region tracking land with later waves. |
| `libfluidcore/workspace/RTreeIndex.h/.cpp` | `class RTreeIndex` | Dynamic R-tree over AABBs with quadratic split (stable handles for insert/remove/update/query). R*-tree heuristics + bulk load for the ROADMAP §5 query-p99 budget arrive in M3. |
| `libfluidcore/workspace/nodes/ExcerptCardNode.cpp` | `class ExcerptCardNode` | Extracted PDF snippet card entity holding source document UUID, page number, normalized rectangle, and text snippet (multi-anchor synthesis enabled). |
| `libfluidcore/workspace/nodes/CardStackNode.cpp` | `class CardStackNode` | Hierarchical accordion container managing collapsible stacks of child excerpt cards. |
| `libfluidcore/squeeze/SqueezeEngine.cpp` | `class SqueezeEngine` | Evaluates the continuous piecewise deformation function $\mathcal{T}(Y_{doc})$ from registered `PageGeometry` data to map document coordinates to screen coordinates. |
| `libfluidcore/graph/GraphTopology.cpp` | `class GraphTopology` | Maintains the bi-directional relational multi-graph $G=(V, E)$ between excerpt cards, notes, and document source anchors. |
| `libfluidcore/graph/ElasticLinkEdge.cpp` | `class ElasticLinkEdge` | Directed link edge computing dynamic cubic Bezier splines between moving workspace nodes. |
| `libfluidcore/storage/ProjectStore.cpp` | `class ProjectStore` | Manages SQLite 3 WAL transactions, `.ltproj` runtime directory bundle mounting, archive packing/unpacking, and schema migrations. |
| `libfluidcore/storage/XoppDocument.h/.cpp` | `structs XoppPoint / XoppStroke / XoppBackground / XoppLayer / XoppPage`, `class XoppDocument` | Legacy `.xopp` bridge (TASK-2.4): pure C++20 model of pages/layers/strokes (points/width/color/tool) plus reader/writer over gzipped `<xournal>` XML using zlib (`parse()` on raw XML, `load()`/`save()` on disk files). Tolerant of unknown attributes and unknown/misplaced elements (forward-compatible); malformed input returns an error via `LoadResult`, never crashes. Clean-room implementation against the file format — no GPL code or symbols linked into the engine. |
| `libfluidcore/storage/AnnotationStore.h/.cpp` | `struct Stroke`, `class AnnotationStore` | Thin C++20 persistence wrapper around `XoppDocument` mapping FluidCore workspace strokes ↔ XoppPage strokes (TASK-2.5). Provides `loadAnnotations()`, `saveAnnotations()`, `addStroke()`, `removeStroke()`, and auto-resolves companion `<file>.xopp` files. Pure engine code with zero GUI/Poppler dependencies (ADR-0001). |
| `libfluidcore/undo/Command.h` | `class Command`, `class CompoundCommand` | Pure C++20 transactional command interfaces for undo/redo state mutations and composite action grouping with automatic rollback on failure (TASK-2.9). |
| `libfluidcore/undo/UndoStack.h/.cpp` | `class UndoStack` | Bounded-capacity Undo/Redo manager (default 100 depth, 64MB byte budget guard) with FIFO capacity trimming, redo truncation, and UI change notification callbacks (TASK-2.9). |
| `libfluidcore/undo/AnnotationCommands.h/.cpp` | `class AddStrokeCommand`, `class RemoveStrokeCommand`, `class ClearPageStrokesCommand` | Concrete annotation commands mutating `AnnotationStore` pages with preserved stroke IDs and geometry (TASK-2.9). |
| `libfluidcore/undo/WorkspaceCommands.h/.cpp` | `class MoveNodeCommand` | Concrete spatial commands mutating `WorkspaceModel` node positions with exact coordinate reversal (TASK-2.9 library groundwork). |
| `libfluidcore/text/TextSelection.h/.cpp` | `struct SelectionRect / PageTextSelection / MultiPageSelectionState`, `class TextSelection` | Pure C++20 domain model and algorithms for text selection bounding boxes, multi-line glyph coalescing into continuous line strips, multi-page selection intervals, clipboard string formatting, and scoped damage box computation (TASK-2.10). Zero GUI/Poppler dependencies (ADR-0001). |
| `libfluidcore/storage/SqliteWalDb.cpp` | `class SqliteWalDb` | Embedded SQLite connection wrapper enforcing WAL journaling and sub-500ms crash-safe debouncing. |
| `libfluidcore/storage/FtsSearchEngine.cpp` | `class FtsSearchEngine` | SQLite FTS5 inverted full-text search engine indexing PDF text streams, excerpt notes, and typed text boxes. |

---

### 2.2 Application Bootstrapping & OS Integration (`src/exe/`)

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| [`src/exe/Xournalpp.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/exe/Xournalpp.cpp) | `main()` | Application entry point. Installs crash handlers, initializes OS console wrappers, sets Pango Cairo backend, and invokes `XournalMain::run()`. |
| [`src/core/control/CrashHandler.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/control/CrashHandler.cpp) | `installCrashHandlers()` | Intercepts POSIX signals (`SIGSEGV`, `SIGFPE`, `SIGILL`, `SIGABRT`) on Linux/macOS and Windows Unhandled Exception Filters on Win32 to trigger emergency save routines. |

---

### 2.3 Core Controller & Coordination Subsystem (`src/core/control/`)

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| [`src/core/control/XournalMain.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/control/XournalMain.cpp) | `class XournalMain` | Coordinates GApplication lifecycle (`startup`, `activate`, `open`, `shutdown`). Parses CLI parameters and initiates project loading. |
| [`src/core/control/Control.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/control/Control.cpp) | `class Control` | Central hub of the application. Owns pointers to `Document`, `MainWindow`, `FluidCoreAPI`, `Settings`, `ToolHandler`, `XournalScheduler`, `PdfCache`, and `UndoRedoHandler`. |
| `src/core/control/SqueezeController.cpp` | `class SqueezeController` | Bridges UI squeeze triggers (touch pinch, `Ctrl+Shift+Scroll`, `Ctrl+Shift+S`, margin fold pins) with `libfluidcore::SqueezeEngine`. |
| [`src/core/control/ToolHandler.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/control/ToolHandler.cpp) | `class ToolHandler` | Manages active tool states (`TOOL_PEN`, `TOOL_HIGHLIGHTER`, `TOOL_ERASER`, `TOOL_SELECT_RECT`, `TOOL_TEXT`, `TOOL_LINK`, `TOOL_HAND`). |

---

### 2.4 GUI Viewports, Widgets & GTK3 Shell (`src/core/gui/`)

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| `app/main.cpp` | `main()`, `AppContext`, `SampleNode` | Wave-2 standalone shell (integration.md §1 scoped down): `GtkApplication` → window → `GtkPaned`; left pane hosts `DocumentPane` (PDF path from `argv[1]`), right pane hosts `WorkspaceView`. Wires `Ctrl+S` (`win.save`), `Ctrl+Z` (`win.undo`), `Ctrl+Shift+Z` / `Ctrl+Y` (`win.redo`), `Ctrl+C` (`win.copy`), `Escape` (clear selection), and `Alt+1`..`Alt+4` tool switching actions. Replaced by the Xournal++ host (`MainWindow::initXournalWidget()` split) in a later wave. |
| `app/DocumentPane.h/.cpp` | `class DocumentPane` | Standalone left-pane document viewport: `GtkPaned` (horizontal draggable divider) hosting `ThumbnailSidebar` on the left and `GtkScrolledWindow` on the right (containing the Poppler PDF `GtkDrawingArea` and interactive `InkOverlay`). Hosts `UndoStack` with scoped damage-rect invalidation on undo/redo. Features `PageTileCache` with LRU byte budgeting and visible-page pinning. Forwards text selection queries and tool changes; cleans up on destruction. Auto-loads companion `<file>.xopp` on open; writes companion `<file>.xopp` on close or explicit save (Ctrl+S). Rendering and presentation only — no business logic in callbacks. |
| `app/PageTileCache.h/.cpp` | `class PageTileCache`, `class CairoSurfaceHandle` | Byte-bounded LRU page raster cache (default 256 MB budget) with RAII refcounted surface handles and visible-page pinning (anti-thrashing) for smooth continuous scrolling without redundant Poppler rendering. |
| `app/ThumbnailSidebar.h/.cpp` | `class ThumbnailSidebar` | Left-hand thumbnail navigation sidebar (`GtkScrolledWindow` hosting custom `GtkDrawingArea`) rendering cached page previews, page number badges, active-page highlight border, and click-to-scroll viewport jump. |
| `app/ThumbnailCache.h/.cpp` | `class ThumbnailCache` | Surface cache storing rendered `cairo_surface_t*` image surfaces per page index to prevent redundant `poppler_page_render` calls during scroll and redraw cycles. |
| `app/ThumbnailLayout.h` | `class ThumbnailLayout` | Pure C++20 geometry engine computing aspect-ratio scaled thumbnail bounding boxes, click hit-testing, and midpoint-to-center active page resolution. Headless and unit-tested via `ThumbnailLayoutTest`. |
| `app/DamageRect.h` | `class DamageRect` | Pure C++20 geometry module calculating pixel-rounded damage bounding boxes for point clicks, stroke segments, and cubic Bezier convex hulls during vector inking. Tested via `DamageRectTest`. |
| `app/StrokeStabilizer.h/.cpp` | `class StrokeStabilizer` | Pure C++20 stabilizer engine implementing Centripetal Catmull-Rom spline fitting, velocity-adaptive deadzone filtering (with dt guards), incremental wet leading-edge streaming, and short-stroke fallbacks (<= 20ms inking latency). Tested via `StrokeStabilizerTest`. |
| `app/TextSelectionService.h/.cpp` | `class TextSelectionService` | Service interfacing with Poppler-GLib text extraction and GTK3 clipboard. Features session glyph layout caching (`poppler_page_get_text_layout`) for sub-millisecond live drag tracking ($<0.05\text{ms}$), glyph-boundary reading-order string extraction via `poppler_page_get_selected_text`, and dual clipboard dispatch (`GDK_SELECTION_CLIPBOARD` and `GDK_SELECTION_PRIMARY`). |
| `app/InkOverlay.h/.cpp` | `class InkOverlay` | Transparent `GtkDrawingArea` overlay on `DocumentPane` capturing pointer/stylus input, streaming through `StrokeStabilizer`, rendering live with wet leading edge and Cairo group alpha isolation, executing eraser hit-testing, rendering text selection highlights with I-beam cursor management, dispatching `AddStrokeCommand`/`RemoveStrokeCommand` to `UndoStack`, and providing scoped dirty-rect redraws. |
| `app/WorkspaceView.h/.cpp` | `class WorkspaceView` | Standalone right-pane canvas: `GtkDrawingArea` whose Cairo pass draws a background grid + node rectangles from `FluidCoreAPI::queryVisibleNodes(viewportBounds)`. Holds the M_view transform (world→screen, identity until M1 pan/zoom). No engine logic in callbacks. |
| [`src/core/gui/MainWindow.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/gui/MainWindow.cpp) | `class MainWindow` | Top-level GTK3 Application window. Houses the `GtkPaned` dual-viewport splitter (`winXournal` document pane and `winWorkspace` canvas). |
| `src/core/gui/workspace/WorkspaceView.cpp` | `class WorkspaceView` | Custom GTK3 widget (`GtkDrawingArea`) rendering the infinite 2D workspace canvas via Cairo, handling spatial panning, pinch zoom ($10\%$ to $1000\%$), and card interactions. |
| `src/core/gui/widgets/ReturnAnchorPill.cpp` | `class ReturnAnchorPill` | Interactive floating overlay component rendered in the document viewport Cairo pass. Receives clicks via custom hit-testing inside the parent `XournalView::button-press-event` to provide instant return navigation to workspace excerpts. |
| [`src/core/gui/PageView.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/gui/PageView.cpp) | `class XojPageView` | View representation of an individual document page. Coordinates on-demand Cairo slice rendering during dynamic squeeze operations. |
| `src/core/gui/sidebar/search/SidebarSearchSlicesPage.cpp` | `class SidebarSearchSlicesPage` | Sidebar tab displaying a sequential stream of search context slices with surrounding sentences extracted via semantic text layout. |

---

### 2.5 Hardware & Input Device Abstraction (`src/core/gui/inputdevices/`)

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| [`src/core/gui/inputdevices/InputContext.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/gui/inputdevices/InputContext.cpp) | `class InputContext` | Connects to GTK widget event signals. Dynamically classifies devices into `PEN`, `ERASER`, `MOUSE`, and `TOUCHSCREEN`. |
| `src/core/gui/inputdevices/DesktopSqueezeHandler.cpp` | `class DesktopSqueezeHandler` | Intercepts `Ctrl+Shift+Scroll` events, `Ctrl+Shift+S` shortcuts, and margin fold pin dragging to trigger document accordion squeezing. |
| [`src/core/gui/inputdevices/TouchInputHandler.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/gui/inputdevices/TouchInputHandler.cpp) | `class TouchInputHandler` | Multi-touch gesture engine supporting two-finger pinch-to-zoom, panning, and two-finger accordion squeeze gestures. |
| [`src/core/gui/inputdevices/HandRecognition.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/gui/inputdevices/HandRecognition.cpp) | `class HandRecognition` | Software palm rejection state machine inhibiting touch gestures during active stylus proximity. |

---

### 2.6 Persistence, File I/O & Serialization

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| `src/core/control/ltproj/LtProjLoader.cpp` | `class LtProjLoader` | Mounts `.ltproj` runtime directory bundles (or unpacks `.ltproj.zip` archives), initializes `libfluidcore::ProjectStore`, and verifies SQLite WAL consistency. |
| `src/core/control/ltproj/LtProjSaver.cpp` | `class LtProjSaver` | Coordinates SQLite WAL checkpoint commits (`PRAGMA wal_checkpoint(TRUNCATE)` before packaging so the archived `-wal` file is empty) and DEFLATE compression via `libzip` for portable standalone `.ltproj.zip` export. |
| [`src/core/control/xojfile/LoadHandler.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/control/xojfile/LoadHandler.cpp) | `class LoadHandler` | Legacy loader for standard `.xopp` / `.xoj` individual document files. Upstream format reference for the clean-room `libfluidcore/storage/XoppDocument` bridge (TASK-2.4). |

---

*This concludes the complete File & Function Architecture Mapping.*
