# Skill: C++ Core Engine Development

## Purpose
Guide correct implementation inside `libfluidcore/` per TRD.md constraints.

## Inputs
- The spec section for the feature (`/specs/features.md` or `system-architecture.md`)
- `src/CONTEXT.md` conventions
- Relevant existing engine module being extended

## Procedure
1. Confirm target module: workspace/ (R-tree scene graph, card stacking & physics), squeeze/ (piecewise mapper), graph/ (topology + Bezier), storage/ (SQLite WAL & .xopp), search/ (squeeze planners), text/ (selection & intervals), undo/ (undo stack & commands)
2. Write the headless unit test FIRST defining expected behavior (e.g., squeeze coordinate mapping invariants)
3. Implement C++20; no GTK/GDK includes; no I/O outside storage/ module
4. Engine API surface changes → update FluidCoreAPI.h docs and `specs/file-function-map.md`
5. Run: `cmake --build build && ctest --test-dir build -R <Module>Test`
6. Spatial/squeeze/render changes → run perf-gate skill before PR

## Done criteria
- New tests pass under ASan/UBSan
- No coverage decrease
- clang-format clean
- Spec updated if behavior differs from doc
