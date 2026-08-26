# Xournal++ Upstream Notes
Last updated: 2026-08-26

## Sibling checkout
`../xournalpp/` contains the upstream source we vendor. Treat it as reference + base.

## Integration principles (from /specs/integration.md)
1. **Minimize edits inside upstream files** — prefer additive modules and interfaces; divergence makes rebases painful (ROADMAP risk register)
2. Fluid-specific code lives either in `libfluidcore/` (engine) or new files under `src/core/gui/workspace/`, `src/core/control/SqueezeController.cpp`, etc.
3. Modified upstream files tracked in `integration.md`; rebase checklist executed per upstream sync
4. Keep GPL-2.0-or-later headers intact in all vendored files

## Useful upstream paths (see file-function-map.md for full map)
- Entry: `src/exe/Xournalpp.cpp` → `src/core/control/XournalMain.cpp`
- Coordination: `src/core/control/Control.cpp`, `ToolHandler.cpp`, scheduler, undo
- Views: `XournalView.cpp`, `PageView.cpp` — our WorkspaceView follows their pattern

## Test assets
`../xournalpp/test/files/` includes sample PDFs/xopp files usable for M1 reader-core testing.
