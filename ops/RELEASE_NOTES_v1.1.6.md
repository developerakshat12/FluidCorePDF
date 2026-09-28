# FluidCore v1.1.6 — Constant-Width Ink, Unified Stroke Renderer, Memory Budget, & Stylus Barrel Buttons

FluidCore **v1.1.6** is an ink-fidelity, memory-governance, and stylus-input release. It also hardens the Linux release pipeline, which had been structurally unable to produce a complete release.

1. **Constant-Width Ink**: Stroke geometry no longer tapers with pressure. A stroke is drawn at exactly the width the user selected, along its entire length.
2. **Unified Stroke Renderer**: All four ink renderers (document pane, infinite canvas, PDF export, excerpt crop tiles) now share a single Cairo rasterizer instead of four disagreeing copies.
3. **Memory Budget**: A single authority partitions the cache budget across page tiles, excerpt tiles, and undo history, sized from physical RAM and trimmed when idle.
4. **Stylus Barrel Buttons**: Barrel presses on a pen now erase instead of panning the canvas or opening a context menu.
5. **Release Pipeline Repair**: The Linux AppImage step could not succeed — its required tools were never installed — and two jobs raced to publish half-releases. Both are fixed.

---

### 🌟 What's New & Fixed in v1.1.6

#### 1. Constant-Width Ink — `StrokeWidthModel`

New canonical header `src/libfluidcore/geometry/StrokeWidthModel.h` is now the single source of truth for ink width, derived by every renderer and by the eraser hit-test.

- **Removed the pressure taper.** Width was previously `baseWidth * (0.25 + 0.75 * p)`, so the selected width meant something different at every point along the same stroke.
- **Fixes a detached blob at pen lift.** On lift, a stroke's tail collapsed toward the `0.25x` floor while the round end cap kept its full radius, leaving what looked like a rendering fault at the end of otherwise clean lines.
- **Fixes save/reload weight drift.** Pressure is still captured and persisted with each stroke but no longer modulates geometry, so a reloaded stroke re-renders at the same apparent weight the user drew.
- **Fixes eraser hit-radius mismatch.** The eraser sized its hit radius from a pressure the ink no longer used.
- **Shared highlighter opacity.** `kHighlighterAlpha = 0.5` is defined once, so a highlight reads identically in the pane, on the canvas, and in exported PDF.
- **NaN-safe clamping.** `renderedWidth()` and `clampPressure()` reject NaN, and a floor of `kMinStrokeWidthPx = 0.5` keeps antialiasing from degenerating into a dotted line.

#### 2. Unified Stroke Renderer — `StrokeRenderer`

New `src/app/services/StrokeRenderer.{h,cpp}` is the single Cairo entry point for ink geometry. The document pane overlay, the infinite canvas, PDF export, and excerpt crop tiles all call it.

- **Fixes polygonal ink at high zoom.** The old code flattened every span into a fixed 3–4 chords and stroked each chord separately. A fixed chord count is resolution dependent, so ink looked smooth at 50% zoom and visibly faceted at 200%.
- **Fixes beaded outlines.** Chained round caps at stepping widths produced a dashed-looking edge. The Centripetal Catmull-Rom spline is now emitted as `cairo_curve_to` spans and stroked **once**, so a single stroke is a single composite and Cairo's stroker subdivides adaptively at any zoom.
- **Fixes export/display drift.** The four renderers had independent copies of the rasterizer that disagreed on both flattening density and the pressure-to-width equation, so exported ink was narrower than what was on screen.
- **Reflection end conditions.** Phantom points reflect past each end of the polyline, so the spline is defined across the whole stroke rather than only between interior samples.

#### 3. Memory Budget — `MemoryBudget`

New `src/app/services/MemoryBudget.{h,cpp}` arbitrates every byte-budgeted consumer in the app.

- **Fixes a 2x budget overrun.** Four independent caches each declared `kDefaultMaxBytes = 64 MB` and each enforced it only against its own surfaces, so a session could legitimately authorize **256 MB** against a documented idle target of **120 MB** (TRD.md §4). Nothing arbitrated between them and nothing ever called `setMaxBytes()`.
- **RAM-derived total.** `recommendedTotalBytes()` derives the pool from physical RAM, clamped to `[16 MB, 512 MB]`, falling back to 64 MB when RAM cannot be determined.
- **Named slices.** `PageTiles`, `ExcerptTiles`, and `UndoStack` each receive an explicit share. The partition is deliberately page-tile heavy, because page tiles back the reading experience the user is looking at right now, excerpt tiles are card thumbnails that re-render cheaply, and undo history is depth-bounded in practice.
- **The two UndoStacks split their slice.** There is an independent stack for the document pane and the workspace canvas, so `undoBytesPerStack()` divides evenly. `UndoStack` lives in `libfluidcore` and must not depend on the GUI (ADR-0001), so the frontend sets each stack's budget explicitly at construction rather than the engine reading the pool.
- **Idle trim and restore.** `trimToIdle()` lowers every slice to 25% of its configured size, releasing cache surfaces back to the allocator instead of holding them until exit; `restoreFromIdle()` puts them back. Both are safe to call repeatedly.

#### 4. Stylus Barrel Buttons — `StylusButtonRouter`

New `src/app/services/StylusButtonRouter.{h,cpp}` maps GDK stylus buttons and device events onto ink intents.

- **Fixes barrel press panning the canvas.** GDK's Win32 backend already surfaces Wintab hardware as ordinary GDK input — a pen arrives as `GDK_SOURCE_PEN` with a `GDK_AXIS_PRESSURE` axis, the eraser end as `GDK_SOURCE_ERASER`, and barrel buttons as plain `GdkEventButton` presses on that pen device. Left alone, a barrel press was indistinguishable from a middle click, so it panned the canvas or opened a context menu instead of erasing.
- **Stylus overrides mouse-oriented tools.** `select`, `text`, `crop`, and `rect_select` are mouse modes that start a selection and consume the press. A stylus now overrides them; otherwise touching the page with a pen while the select tool was active selected text instead of drawing, forcing a mouse tool switch before every annotation.
- **Eraser is honoured as selected.** An earlier revision overrode unconditionally, which meant a stylus press with the eraser selected drew a stroke at the eraser's forced width instead of erasing.
- **Barrel-button state machine.** `isBarrelButton()` lets the eraser be held for the duration of a press.
- **No Windows Ink required.** This is plain GDK input routing, the layer the rest of the app is already built on.

#### 5. Release Pipeline Repair

The Linux release could not have succeeded, and this is the direct cause of repeated re-release attempts.

- **AppImage step was guaranteed to fail.** `appimagetool` hard-`die()`s with *"mksquashfs command is missing but required"* and *"desktop-file-validate command is missing but required"* before doing any work. Neither `squashfs-tools` nor `desktop-file-utils` was in the runner's package list, so no AppImage was ever produced. Both are now installed in `ci.yml` and `release.yml`.
- **Two jobs raced to publish.** `release-windows` and `release-linux` each called `softprops/action-gh-release` for the same tag with no ordering. The first to finish published a live release containing only 3 of 6 assets, and a simultaneous create could return `422 already_exists`. Release is now a single `publish` job gated on `needs:`, with `fail_on_unmatched_files: true` so a missing asset fails loudly instead of shipping a partial release.
- **Node 20 actions can no longer run.** GitHub removed Node 20 from hosted runners on 2026-09-23 with no opt-out, which stranded `actions/checkout@v4`, `actions/upload-artifact@v4`, and `softprops/action-gh-release@v2`. All are on Node 24 majors now.
- **`ubuntu-22.04` is deprecated.** The image entered deprecation on 2026-09-17 with hard brownouts scheduled before its 2027-04-17 retirement. Both workflows are pinned to `ubuntu-24.04`; `ubuntu-latest` is also pinned because it migrates to `ubuntu-26.04` between 2026-10-19 and 2026-11-19.
- **Portable zip used Windows path separators.** `Compress-Archive` under Windows PowerShell 5.1 writes `share\glib-2.0\schemas\gschemas.compiled` into the archive, which violates the ZIP specification. Non-Windows extractors materialised those as single flat files instead of a directory tree. The packager now writes entries by hand and then **verifies** that no entry contains a backslash and that the entry count matches.
- **Windows checksums were a truncated table.** `Get-FileHash | Out-File` emits a formatted table whose `Path` column is ellipsis-truncated at console width, and it is not parseable by `sha256sum -c`. Both platforms now emit a standard `<hash>  <bare-filename>` file.
- **Inno Setup could not accept a path with spaces.** `/DSourceDistDir=` and `/DOutputDir=` were passed unquoted through a space-joining argument list, and `fluidcore.iss` used an unquoted `OutputDir=`. Both are now quoted.
- **Cairo pin now fails loudly.** `curl -sSL` had no `--fail`, so a 404 body was written to disk with exit 0 and `pacman` then failed on a non-zstd file. The pin is `-fsSL` and short-circuits when the installed Cairo is already the pinned version.
- **Added the missing MSYS2 dependency.** `mingw-w64-ucrt-x86_64-gtk-update-icon-cache` was required by `CLAUDE.md` and `ops/CONTEXT.md` but absent from both workflows, leaving the bundled `share\icons\hicolor` tree without a cache.
- **Added `.gitattributes`.** Without it, Windows checkouts silently rewrite LF to CRLF, which breaks `ops/scripts/*.sh` (*bad interpreter: `bash^M`*) and makes `ops/patches/*.patch` unapplyable because its context lines no longer match. Line endings are now pinned per file type.
- **Added `concurrency`, `timeout-minutes`, and least-privilege `permissions`** to both workflows, and `workflow_dispatch` to `release.yml` so a release can be re-run without deleting and re-pushing a tag.

---

### 📦 Platform Binaries & Packaging

- **Windows 11 (Native)**:
  - Inno Setup installer: `FluidCore-Setup-x64.exe` (v1.1.6)
  - Portable zip: `fluidcore-windows-x64.zip` (v1.1.6) — now POSIX-separated
  - Checksums: `checksums-windows-sha256.txt` (now `sha256sum -c` compatible)
  - Resources: `src/app/fluidcore.rc` version set to `1.1.6.0`
- **Linux (Ubuntu / Debian / Fedora / Arch)**:
  - AppImage: `FluidCore-1.1.6-x86_64.AppImage`
  - Debian package: `fluidcore_1.1.6_amd64.deb`
  - Checksums: `checksums-linux-sha256.txt`
  - AppStream metainfo: `resources/linux/org.fluidcore.platform.metainfo.xml` updated with v1.1.6 release metadata

---

### 🧪 Verification & Test Results

- **Windows Ninja Build**: Clean build with `ops\scripts\build-win.ps1` via MSYS2 UCRT64, Cairo pinned to `1.18.4-4`.
- **CTest Suite**: 42/42 tests passed (100% pass rate), including 3 new suites — `StrokeRendererTest`, `MemoryBudgetTest`, `StylusButtonRouterTest` — plus extended `ExcerptTileCacheTest` and `PageTileCacheTest`.
- **New coverage**:
  - `StrokeRendererTest` — width constancy, NaN rejection, zoom-independent smoothness, pane/export parity.
  - `MemoryBudgetTest` — slice partitioning, RAM-derived clamping, idle trim/restore round-trip.
  - `StylusButtonRouterTest` — barrel-button classification, tool override precedence, genuine mouse passthrough.
- **Memory & Scalability**: `ScalabilityBenchmarkTest` passes against the $\le 1.2\text{ GB}$ ceiling.
- **Offline guarantee**: Zero runtime network calls; the Cairo pin and appimagetool download are build-time only.
