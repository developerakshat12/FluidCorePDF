# Src Workspace (Code)

Last updated: 2026-08-26

## What this is for
The actual codebase. Currently scaffolded — M0 bootstrap fills it.

```
src/
├── libfluidcore/   # Standalone C++20 engine. NO GTK HEADERS EVER (ADR-0001)
│   ├── workspace/  # Scene graph + R*-tree spatial index
│   ├── squeeze/    # Piecewise coordinate mapper Y_screen = T(Y_doc, SqueezeRegions)
│   ├── graph/      # GraphTopology G=(V,E), cubic Bezier router
│   └── storage/    # SQLite WAL ProjectStore (.ltproj bundle)
└── app/            # GTK 3 / Cairo / Poppler frontend (Xournal++ based)
```

## Conventions
- C++20; CMake ≥ 3.20 + Ninja; GCC ≥ 12 / Clang ≥ 16
- Files PascalCase matching class (`SqueezeEngine.cpp/h`); dirs lowercase
- Tests mirror source path: `libfluidcore/squeeze/SqueezeEngineTest.cpp` (headless, no GUI deps)
- Engine changes REQUIRE unit tests; coverage may not decrease
- Formatting via `.clang-format` (`make format`) — enforced in CI

## Process (new work)
1. Route from CLAUDE.md; read the matching spec in `/specs` first
2. Engine work: implement in `libfluidcore/` + headless test; GUI work: thin adapter over FluidCoreAPI.h
3. Anything touching squeeze/spatial-index/render paths → attach benchmark artifact to PR (two approvals needed, GOVERNANCE §2)
4. Update `file-function-map.md` in `/specs` when adding modules

## What good looks like
PRs are vertical slices: engine change + test + (if visible) frontend wiring + spec touch-up.

## Avoid
- GPL-only symbols linked into `libfluidcore/` (blocks future relicensing)
- Business logic in GTK callbacks — it belongs in the engine or controller layer
- Comments restating code; explain *why* only
