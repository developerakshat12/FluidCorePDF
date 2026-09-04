# app (GTK Frontend)

GTK 3 / Cairo / Poppler desktop frontend consuming `libfluidcore` via `FluidCoreAPI.h`.

Current layout:
```
app/
├── CMakeLists.txt
├── main.cpp                  # Application bootstrap and dual-pane container setup
├── document/                 # Document viewport & overlays
│   ├── DocumentPane.h/.cpp       # Poppler PDF scrolled window viewport with coordinate projection
│   ├── InkOverlay.h/.cpp         # Live stylus/pointer capture, stabilizer integration, crop marquee
│   ├── ReturnAnchorPill.h/.cpp   # Floating glassmorphic return anchor capsule
│   ├── SearchBarWidget.h/.cpp    # Real-time search query input and match navigation
│   ├── SqueezeRenderHelper.h/.cpp# Cairo slice decomposition, crease shadows, margin fold pins
│   └── DamageRect.h              # Scoped bounding-box invalidation math
├── export/                   # Export dialogs & progress tracking
│   ├── ExportDialog.h/.cpp       # Multi-format modal selector (.pdf, .md) with page range options
│   └── ExportProgressDialog.h/.cpp# Asynchronous export progress tracking with cancel handle
├── services/                 # Background workers and subsystem services
│   ├── DocumentSearchService.h/.cpp # Async multi-page search worker
│   ├── ExcerptTileCache.h/.cpp      # 128 MB LRU excerpt raster cache across discrete LoD tiers
│   ├── PageTileCache.h/.cpp         # 256 MB LRU page surface cache with visible-page pinning
│   ├── PdfDocumentService.h/.cpp    # Thread-safe Poppler document handle lifecycle
│   ├── PdfExportService.h/.cpp      # Asynchronous Cairo vector PDF flattening worker with atomic swap
│   ├── StrokeStabilizer.h/.cpp      # Centripetal Catmull-Rom spline filter with deadzone
│   ├── TextSelectionService.h/.cpp  # Reading-order glyph layout extraction & clipboard
│   └── ToolManager.h/.cpp           # Single-source-of-truth tool state synchronization service
├── workspace/                # Infinite 2D synthesis canvas & toolbar
│   ├── TopToolbarWidget.h/.cpp      # Floating pill toolbar (tools, undo/redo, zoom, minimap, search, export)
│   ├── WorkspaceView.h/.cpp         # GTK3 drawing area coordinator, undo routing & input router
│   ├── WorkspaceRenderer.h/.cpp     # Cairo rendering passes (cards, stacks, Bezier links, minimap)
│   ├── WorkspaceInteraction.h/.cpp  # Spatial hit-testing, drag-and-drop, context menus
│   └── WorkspaceState.h             # Authoritative viewport transform and interaction state
└── tests/                    # GUI and service test suites
    ├── BiDirectionalAnchorTest.cpp
    ├── CropDragCrashTest.cpp
    ├── DamageRectTest.cpp
    ├── ExcerptTileCacheTest.cpp
    ├── PageTileCacheTest.cpp
    ├── PdfExportServiceTest.cpp
    ├── ReturnAnchorPillTest.cpp
    ├── SearchSqueezePlannerTest.cpp
    ├── SqueezeRenderTest.cpp
    ├── StrokeStabilizerTest.cpp
    └── WorkspaceInteractionTest.cpp
```

Rules: business logic never in GTK event callbacks; every interaction has keyboard/mouse parity; dirty-rect rendering with LRU cache invalidation.

