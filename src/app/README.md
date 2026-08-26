# app (GTK Frontend)

GTK 3 / Cairo / Poppler desktop frontend extending Xournal++, consuming `libfluidcore` via `FluidCoreAPI.h`.

Planned layout:
```
app/
├── CMakeLists.txt
├── exe/                  # entry point (Xournalpp.cpp)
├── control/              # controllers: Control hub, ToolHandler, SqueezeController, jobs
├── gui/                  # MainWindow (GtkPaned), XournalView, WorkspaceView, PageView,
│                         # ReturnAnchorPill, input handlers (pen/touch/desktop squeeze)
└── undo/                 # UndoRedoHandler extended for fluid objects
```

Rules: business logic never in event callbacks; every interaction has keyboard/mouse parity; dirty-rect rendering only.
