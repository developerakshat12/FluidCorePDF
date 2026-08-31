# Src Workspace (Code)

Last updated: 2026-08-31

## What this is for
The core codebase for the FluidCore platform. Milestones M0–M3 are complete, and M4 (Links, Stacks & Search) is currently in progress.

```
src/
├── libfluidcore/               # Standalone C++20 engine. NO GTK/GDK/Cairo HEADERS EVER (ADR-0001)
│   ├── workspace/              # Spatial scene graph (WorkspaceModel, ExcerptCardNode, CardStackNode, CanvasStrokeNode, CardLayoutEngine, PhysicsSolver, RTreeIndex, ExcerptPayload)
│   ├── squeeze/                # Piecewise coordinate mapper (SqueezeEngine)
│   ├── graph/                  # Relational multigraph & Bezier routing (GraphTopology, GraphEdge)
│   ├── storage/                # SQLite WAL ProjectStore, AnnotationStore, XoppDocument (.xopp bridge)
│   ├── search/                 # AnchorSqueezePlanner, SearchSqueezePlanner
│   ├── text/                   # TextSelection domain model & interval arithmetic
│   ├── undo/                   # UndoStack, Command, Annotation/Squeeze/Workspace command primitives
│   ├── tests/                  # Headless CTest suites (storage/, workspace/, engine, topology, squeeze, text, undo)
│   ├── FluidCoreAPI.h          # Public abstract engine facade interface
│   ├── FluidCoreEngine.h/.cpp  # Concrete engine implementation
│   └── CMakeLists.txt
└── app/                        # GTK 3 / Cairo / Poppler desktop frontend
    ├── document/               # DocumentPane (Poppler PDF viewport), InkOverlay, ReturnAnchorPill, SearchBarWidget, SqueezeRenderHelper, DamageRect
    ├── services/               # DocumentSearchService, ExcerptTileCache, PageTileCache, PdfDocumentService, StrokeStabilizer, TextSelectionService
    ├── workspace/              # Infinite canvas: WorkspaceView, WorkspaceRenderer, WorkspaceInteraction, WorkspaceState
    ├── tests/                  # Viewport, tile cache, stabilizer, anchor, and interaction tests
    ├── main.cpp                # GTK3 application entry point
    └── CMakeLists.txt
```

## Conventions
- C++20; CMake ≥ 3.20 + Ninja; GCC ≥ 12 / Clang ≥ 16
- Files PascalCase matching class (`SqueezeEngine.cpp/h`); dirs lowercase
- Tests mirror source path: `libfluidcore/squeeze/SqueezeEngineTest.cpp` (headless, no GUI deps) and `app/tests/` (GUI/service tests)
- Engine changes REQUIRE unit tests; coverage may not decrease
- Formatting via `.clang-format` (`make format` / `clang-format`) — enforced in CI

## Process (new work)
1. Route from CLAUDE.md; read the matching spec in `/specs` first
2. Engine work: implement in `libfluidcore/` + headless test; GUI work: modular adapters in `app/document/`, `app/services/`, or `app/workspace/` over `FluidCoreAPI.h`
3. Anything touching squeeze/spatial-index/render paths → attach benchmark artifact to PR (two approvals needed, GOVERNANCE §2)
4. Update `file-function-map.md` in `/specs` when adding or modifying modules

## What good looks like
PRs are vertical slices: engine change + test + (if visible) frontend wiring + spec touch-up.

## Avoid
- GPL-only symbols linked into `libfluidcore/` (blocks future relicensing)
- Business logic in GTK callbacks — it belongs in the engine or controller/service layer
- Comments restating code; explain *why* only

