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
| `app/main.cpp` | `main()`, `SampleNode` | Wave-1 standalone shell (integration.md §1 scoped down): `GtkApplication` → window → `GtkPaned`; left pane placeholder, right pane hosts `WorkspaceView`. Seeds demo nodes through the abstract `FluidCoreAPI` boundary only. Replaced by the Xournal++ host (`MainWindow::initXournalWidget()` split) in Wave 2. |
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
| [`src/core/control/xojfile/LoadHandler.cpp`](file:///c:/Users/ASUS/OneDrive/Desktop/Reference%20Repos/xournalpp/src/core/control/xojfile/LoadHandler.cpp) | `class LoadHandler` | Legacy loader for standard `.xopp` / `.xoj` individual document files. |

---

*This concludes the complete File & Function Architecture Mapping.*
