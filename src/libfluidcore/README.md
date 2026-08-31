# libfluidcore

Standalone C++20 engine. **No GTK/GDK/Cairo includes — ever.** See ADR-0001.

Current layout:
```
libfluidcore/
├── CMakeLists.txt            # Standalone target, zero GUI/Poppler dependencies
├── FluidCoreAPI.h            # Public pure abstract interface consumed by frontend
├── FluidCoreEngine.h/.cpp    # Concrete engine facade implementation
├── workspace/                # Spatial scene graph & card physics
│   ├── WorkspaceModel.h/.cpp     # Authoritative node registry & spatial operations
│   ├── RTreeIndex.h/.cpp         # Dynamic R-tree spatial index over AABBs
│   ├── ExcerptCardNode.h/.cpp    # Extracted document snippet entity
│   ├── CardStackNode.h/.cpp      # Hierarchical container for card stacks
│   ├── CanvasStrokeNode.h/.cpp   # Freehand ink stroke entity on canvas
│   ├── CardLayoutEngine.h/.cpp   # Accordion cascade deck positioning
│   ├── PhysicsSolver.h/.cpp      # Proximity snapping (16pt) and overlap detection
│   └── ExcerptPayload.h/.cpp     # Binary & string serialization for drag-and-drop
├── squeeze/                  # Piecewise continuous deformation engine
│   └── SqueezeEngine.h/.cpp      # Piecewise mapper Y_screen = T(Y_doc, regions)
├── graph/                    # Relational knowledge multigraph
│   ├── GraphTopology.h/.cpp      # Multigraph G=(V,E) maintaining bi-directional links
│   └── GraphEdge.h               # Directed edge representation with Bezier spline anchors
├── storage/                  # Persistence layer
│   ├── ProjectStore.h/.cpp       # SQLite WAL project bundle manager (.ltproj)
│   ├── AnnotationStore.h/.cpp    # High-level stroke and annotation persistence
│   └── XoppDocument.h/.cpp       # Clean-room .xopp XML/gzip reader and writer
├── search/                   # Search and squeeze planning algorithms
│   ├── WorkspaceSearchEngine.h/.cpp # In-memory text, title, and tag search (#tag, tag:xyz)
│   ├── AnchorSqueezePlanner.h/.cpp  # Multi-anchor interval union & context padding
│   └── SearchSqueezePlanner.h/.cpp  # Search hit interval expansion & page folding
├── export/                   # Synthesis export engines
│   └── WorkspaceExportEngine.h/.cpp # Pure C++20 Markdown outline & Mermaid graph serializer
├── text/                     # Text selection domain logic
│   └── TextSelection.h/.cpp      # Glyph layout intervals, line strip coalescing
├── undo/                     # Transactional undo/redo subsystem
│   ├── UndoStack.h/.cpp          # Capacity-bounded undo/redo stack
│   ├── Command.h                 # Pure virtual Command and CompoundCommand base classes
│   ├── AnnotationCommands.h/.cpp # Add/remove stroke commands
│   ├── SqueezeCommands.h/.cpp    # Squeeze state mutation commands
│   └── WorkspaceCommands.h/.cpp  # Node insertion, movement, deletion commands
└── tests/                    # Headless CTest suites (zero GUI deps)
    ├── storage/                  # AnnotationStoreTest, XoppDocumentTest
    ├── workspace/                # CardLayoutEngineTest, CardStackNodeTest, ExcerptCardNodeTest, PhysicsSolverTest, RTreeBenchmarkTest, RTreeIndexTest, WorkspaceModelTest
    ├── AnchorSqueezePlannerTest.cpp
    ├── FluidCoreApiSmokeTest.cpp
    ├── FluidCoreEngineTest.cpp
    ├── GraphTopologyTest.cpp
    ├── SearchSqueezePlannerTest.cpp
    ├── SqueezeEngineTest.cpp
    ├── SqueezeRenderTest.cpp
    ├── TextSelectionTest.cpp
    ├── UndoStackTest.cpp
    ├── WorkspaceExportEngineTest.cpp
    └── WorkspaceSearchEngineTest.cpp
```

Rules: every module ships with headless unit tests; perf-gated paths (squeeze, spatial index, rendering math) require benchmark validation.

