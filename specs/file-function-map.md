# Xournal++ & FluidCore File & Function Architecture Mapping

This document provides a comprehensive map of the FluidCore platform codebase, detailing how source files, headers, and classes are structured in the **Decoupled C++20 Core Engine (`libfluidcore`)** and the **GTK 3 / Cairo Frontend (`src/app`)**, what responsibilities each component handles, and how they interconnect across subsystems.

---

## 1. Subsystem Interconnection Matrix

```mermaid
graph TD
    subgraph Frontend Application Shell (src/app/)
        MAIN["src/app/main.cpp"] --> DOC_PANE["src/app/document/DocumentPane.cpp"]
        MAIN --> WS_VIEW["src/app/workspace/WorkspaceView.cpp"]
        
        DOC_PANE --> INK_OVL["src/app/document/InkOverlay.cpp"]
        DOC_PANE --> SEARCH_BAR["src/app/document/SearchBarWidget.cpp"]
        DOC_PANE --> RETURN_PILL["src/app/document/ReturnAnchorPill.cpp"]
        DOC_PANE --> SQ_RENDER["src/app/document/SqueezeRenderHelper.cpp"]
        
        WS_VIEW --> WS_RENDER["src/app/workspace/WorkspaceRenderer.cpp"]
        WS_VIEW --> WS_INTERACT["src/app/workspace/WorkspaceInteraction.cpp"]
        WS_VIEW --> WS_STATE["src/app/workspace/WorkspaceState.h"]
        
        DOC_PANE --> PDF_SVC["src/app/services/PdfDocumentService.cpp"]
        DOC_PANE --> PAGE_CACHE["src/app/services/PageTileCache.cpp"]
        DOC_PANE --> DOC_SEARCH["src/app/services/DocumentSearchService.cpp"]
        INK_OVL --> STABILIZER["src/app/services/StrokeStabilizer.cpp"]
        INK_OVL --> TXT_SVC["src/app/services/TextSelectionService.cpp"]
        WS_VIEW --> EXCERPT_CACHE["src/app/services/ExcerptTileCache.cpp"]
    end

    subgraph Decoupled Core Engine Interface
        DOC_PANE --> FC_API["libfluidcore/FluidCoreAPI.h"]
        WS_VIEW --> FC_API
        FC_API --> FC_ENG["libfluidcore/FluidCoreEngine.cpp"]
    end

    subgraph Decoupled Core Engine (src/libfluidcore / C++20)
        FC_ENG --> WS_MODEL["libfluidcore/workspace/WorkspaceModel.cpp"]
        FC_ENG --> SQ_ENG["libfluidcore/squeeze/SqueezeEngine.cpp"]
        FC_ENG --> GRAPH_TOP["libfluidcore/graph/GraphTopology.cpp"]
        FC_ENG --> PROJ_STORE["libfluidcore/storage/ProjectStore.cpp"]
        FC_ENG --> ANNOT_STORE["libfluidcore/storage/AnnotationStore.cpp"]
        FC_ENG --> UNDO_STACK["libfluidcore/undo/UndoStack.cpp"]
        
        WS_MODEL --> RTREE["libfluidcore/workspace/RTreeIndex.cpp"]
        WS_MODEL --> PHYSICS["libfluidcore/workspace/PhysicsSolver.cpp"]
        WS_MODEL --> CARD_LAYOUT["libfluidcore/workspace/CardLayoutEngine.cpp"]
        
        SQ_ENG --> ANCHOR_PLANNER["libfluidcore/search/AnchorSqueezePlanner.cpp"]
        DOC_SEARCH --> SEARCH_PLANNER["libfluidcore/search/SearchSqueezePlanner.cpp"]
        
        ANNOT_STORE --> XOPP_DOC["libfluidcore/storage/XoppDocument.cpp"]
    end
```

---

## 2. Granular Module Breakdown

### 2.1 Decoupled Core Engine (`src/libfluidcore/`)

> Current status: Milestones M0–M3 complete, M4 (Links, Stacks & Search) in progress.
> All engine modules are pure C++20 with zero GUI/Cairo/GTK dependencies (ADR-0001) and ship with headless CTest test suites.

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| `libfluidcore/FluidCoreAPI.h` | `class FluidCoreAPI` | Pure abstract C++ interface exposing document geometry registration, coordinate transforms, UUID-based node registry, spatial range queries, graph edges, and pure geometry getters (node bounds, positions, spline control points) to the GUI frontend. No Cairo/GTK types cross this boundary. |
| `libfluidcore/FluidCoreEngine.h/.cpp` | `class FluidCoreEngine : public FluidCoreAPI` | Concrete facade consumed by the GUI. Coordinates `WorkspaceModel`, `SqueezeEngine`, `GraphTopology`, `ProjectStore`, and `UndoStack`. |
| `libfluidcore/workspace/WorkspaceModel.h/.cpp` | `class WorkspaceModel` | Authoritative spatial scene graph managing polymorphic `WorkspaceNode` entities keyed by UUIDs, tracking positions, z-ordering, bounds, and viewport queries via the R-tree index. |
| `libfluidcore/workspace/RTreeIndex.h/.cpp` | `class RTreeIndex` | Dynamic R-tree spatial index over AABBs with quadratic split algorithm providing $O(\log N)$ viewport culling and sub-millisecond range queries. |
| `libfluidcore/workspace/ExcerptCardNode.h/.cpp` | `class ExcerptCardNode` | Extracted PDF snippet card entity holding source document UUID, page number, normalized source rectangle, crop bounds, and text snippet. |
| `libfluidcore/workspace/CardStackNode.h/.cpp` | `class CardStackNode` | Hierarchical accordion container managing collapsible stacks of child excerpt cards with compound translation and collapse state. |
| `libfluidcore/workspace/CanvasStrokeNode.h/.cpp` | `class CanvasStrokeNode` | Freehand vector ink stroke entity on the infinite canvas with spatial bounding box and point collections. |
| `libfluidcore/workspace/CardLayoutEngine.h/.cpp` | `class CardLayoutEngine` | Accordion cascade deck positioning and expanded/collapsed child card offsets. |
| `libfluidcore/workspace/PhysicsSolver.h/.cpp` | `class PhysicsSolver` | Pure C++20 proximity snapping (16pt magnetic threshold), overlap calculations, and stack-merge detection. |
| `libfluidcore/workspace/ExcerptPayload.h/.cpp` | `struct ExcerptPayload` | Binary and string serialization/deserialization helper for drag-and-drop excerpt transfer. |
| `libfluidcore/squeeze/SqueezeEngine.h/.cpp` | `class SqueezeEngine` | Evaluates the continuous piecewise deformation function $\mathcal{T}(Y_{doc})$ to map document coordinates to screen coordinates with multi-segment compression and layered folds. |
| `libfluidcore/graph/GraphTopology.h/.cpp` | `class GraphTopology` | Maintains the bi-directional relational multigraph $G=(V, E)$ between excerpt cards, notes, and document source anchors. |
| `libfluidcore/graph/GraphEdge.h` | `struct GraphEdge`, `enum EdgeDirection` | Directed relational link edge representation connecting source and target nodes with cubic Bezier control points. |
| `libfluidcore/search/AnchorSqueezePlanner.h/.cpp` | `class AnchorSqueezePlanner` | Computes interval unions with context padding around active workspace anchor points for automated document unfolding. |
| `libfluidcore/search/SearchSqueezePlanner.h/.cpp` | `class SearchSqueezePlanner` | Computes uncollapsed search hit intervals with context margin expansion for search-driven squeeze modes. |
| `libfluidcore/text/TextSelection.h/.cpp` | `struct SelectionRect`, `struct PageTextSelection`, `class TextSelection` | Pure C++20 domain model and algorithms for text selection bounding boxes, multi-line glyph coalescing into continuous line strips, multi-page selection intervals, and clipboard formatting. |
| `libfluidcore/storage/ProjectStore.h/.cpp` | `class ProjectStore` | Manages SQLite 3 WAL transactions, `.ltproj` runtime directory bundle mounting, archive packing/unpacking, and schema migrations. |
| `libfluidcore/storage/AnnotationStore.h/.cpp` | `struct Stroke`, `class AnnotationStore` | Thin C++20 persistence wrapper around `XoppDocument` mapping FluidCore workspace strokes ↔ XoppPage strokes. Auto-resolves companion `<file>.xopp` files. |
| `libfluidcore/storage/XoppDocument.h/.cpp` | `structs XoppPoint / XoppStroke / XoppLayer / XoppPage`, `class XoppDocument` | Clean-room C++20 model of pages/layers/strokes plus reader/writer over gzipped `<xournal>` XML using zlib (`parse()`, `load()`, `save()`). |
| `libfluidcore/undo/Command.h` | `class Command`, `class CompoundCommand` | Pure C++20 transactional command interfaces for undo/redo state mutations with automatic rollback on failure. |
| `libfluidcore/undo/UndoStack.h/.cpp` | `class UndoStack` | Bounded-capacity Undo/Redo manager (default 100 depth, 64MB memory budget guard) with FIFO trimming and UI notification callbacks. |
| `libfluidcore/undo/AnnotationCommands.h/.cpp` | `class AddStrokeCommand`, `class RemoveStrokeCommand`, `class ClearPageStrokesCommand` | Concrete annotation commands mutating `AnnotationStore` pages with preserved stroke IDs and geometry. |
| `libfluidcore/undo/SqueezeCommands.h/.cpp` | `class SqueezeCommand` | Squeeze state mutation commands for undo/redo integration. |
| `libfluidcore/undo/WorkspaceCommands.h/.cpp` | `class MoveNodeCommand`, `class InsertNodeCommand`, `class RemoveNodeCommand` | Concrete spatial commands mutating `WorkspaceModel` node positions and scene graph membership. |

---

### 2.2 Desktop Frontend Viewports & Widgets (`src/app/document/` & `src/app/workspace/`)

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| `src/app/main.cpp` | `main()` | Application entry point. Instantiates `GtkPaned` dual-viewport layout hosting `DocumentPane` (left) and `WorkspaceView` (right). |
| `src/app/document/DocumentPane.h/.cpp` | `class DocumentPane` | Standalone left-pane document viewport: `GtkScrolledWindow` hosting Poppler PDF drawing area, `SearchBarWidget`, and interactive `InkOverlay`. Hosts `UndoStack` and `PageTileCache`. Auto-loads/saves `.xopp` companion files. |
| `src/app/document/DamageRect.h` | `class DamageRect` | Pure C++20 geometry helper calculating pixel-rounded damage bounding boxes for point clicks, stroke segments, and cubic Bezier convex hulls during vector inking. |
| `src/app/document/InkOverlay.h/.cpp` | `class InkOverlay` | Transparent `GtkDrawingArea` overlay on `DocumentPane` capturing stylus/pointer input, streaming through `StrokeStabilizer`, rendering live wet leading edge, eraser hit-testing, marquee crop selection, and text selection highlights. |
| `src/app/document/ReturnAnchorPill.h/.cpp` | `class ReturnAnchorPill`, `struct ReturnAnchorPillGeometry` | Interactive floating viewport overlay component in `DocumentPane` rendering dark glassmorphic capsule with return icon `↶`, excerpt badge, close button `✕`, and return navigation dispatch. |
| `src/app/document/SearchBarWidget.h/.cpp` | `class SearchBarWidget` | Search query entry widget with real-time match counters, navigation buttons, and integration with `DocumentSearchService`. |
| `src/app/document/SqueezeRenderHelper.h/.cpp` | `class SqueezeRenderHelper` | Cairo rendering helper for squeeze margin bands, fold lines, and deformation grid overlays. |
| `src/app/workspace/WorkspaceView.h/.cpp` | `class WorkspaceView` | Slim GTK3 drawing area coordinator owning `WorkspaceState`, routing input signals to `WorkspaceInteraction`, delegating draw passes to `WorkspaceRenderer`, and managing GLib animation timers. |
| `src/app/workspace/WorkspaceRenderer.h/.cpp` | `class WorkspaceRenderer` | Pure Cairo rendering passes for infinite canvas: background dot grid, cards, collapsible stacks, Bezier graph links, snapping guides, ghost merge preview, active ink strokes, and minimap HUD. |
| `src/app/workspace/WorkspaceInteraction.h/.cpp` | `class WorkspaceInteraction` | Hit-testing routines (nodes, child stack items, Bezier splines, minimap), context menus, stack rename dialogs, and GTK drag-and-drop excerpt ingestion. |
| `src/app/workspace/WorkspaceState.h` | `struct WorkspaceState`, `struct ViewportTransform`, `struct DragSnapState`, `struct InkingState` | Authoritative state container decoupling viewport matrix, in-progress drag/snapping/ghost bounds, stroke stabilization, selection, and animations. |

---

### 2.3 Subsystem Services (`src/app/services/`)

| File / Path | Key Symbols & Classes | Primary Responsibilities & Connectivity |
|---|---|---|
| `src/app/services/DocumentSearchService.h/.cpp` | `class DocumentSearchService` | Background thread document search worker with match caching and regex/case-insensitive scanning. |
| `src/app/services/ExcerptTileCache.h/.cpp` | `class ExcerptTileCache`, `struct CropCacheKey`, `class CairoSurfaceHandle` | 128 MB byte-bounded LRU surface cache rasterizing cropped high-DPI PDF document excerpts across discrete LoD zoom tiers using `GThreadPool` background workers. |
| `src/app/services/PageTileCache.h/.cpp` | `class PageTileCache`, `class CairoSurfaceHandle` | Byte-bounded LRU page raster cache (default 256 MB budget) with RAII refcounted surface handles and visible-page pinning for smooth continuous scrolling. |
| `src/app/services/PdfDocumentService.h/.cpp` | `class PdfDocumentService` | Multi-document resolution and concurrency isolation service managing RAII Poppler handles with dedicated worker synchronization. |
| `src/app/services/StrokeStabilizer.h/.cpp` | `class StrokeStabilizer` | Pure C++20 stabilizer engine implementing Centripetal Catmull-Rom spline fitting, velocity-adaptive deadzone filtering, and incremental wet leading-edge streaming. |
| `src/app/services/TextSelectionService.h/.cpp` | `class TextSelectionService` | Service interfacing with Poppler-GLib text extraction and GTK3 clipboard, glyph layout caching, and reading-order string extraction. |

---

*This concludes the complete File & Function Architecture Mapping.*

