# libfluidcore

Standalone C++20 engine. **No GTK/GDK includes — ever.** See ADR-0001.

Planned layout (fills in at M0):
```
libfluidcore/
├── CMakeLists.txt        # standalone target, no GUI deps
├── FluidCoreAPI.h        # public API surface consumed by src/app
├── workspace/            # WorkspaceModel scene graph + R*-tree spatial index
├── squeeze/              # SqueezeEngine piecewise mapper Y_screen = T(Y_doc, regions)
├── graph/                # GraphTopology G=(V,E), cubic Bezier router
└── storage/              # ProjectStore: .ltproj bundle over SQLite WAL + FTS5
```

Rules: every module ships with headless unit tests; perf-gated paths (squeeze, workspace index) require benchmark artifacts on change.
