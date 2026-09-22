# FluidCore v1.1.4 — Spacebar Minimap Peeking, Clipboard Image Pasting, R-Tree Robustness, & Persistence Hardening

FluidCore **v1.1.4** is a major stability, usability, and bug fix release focused on canvas workflow speed, multimodal content import, spatial query correctness, and project persistence guarantees:

1. **Temporary Spacebar Minimap Peeking**: Hold down `Spacebar` to temporarily reveal and inspect the minimap radar overlay at any time; releasing `Spacebar` automatically restores your canvas view without disrupting your active tool or workflow.
2. **Native Clipboard Image Pasting (`Ctrl+V`) & Drag-and-Drop Raster Images**: Paste screenshots and images directly from your system clipboard or drag-and-drop image files (PNG, JPG, WEBP, etc.) straight onto the canvas. Images are saved into project assets, automatically scaled with aspect ratio preservation, and registered as excerpt cards with full Undo/Redo support.
3. **External PDF Path Retention & Robust Document Resolution**: Projects now persist and restore absolute external paths (`external_path`) alongside bundle-relative paths for referenced documents. Multi-strategy document resolution ensures annotations and excerpt crops never detach even across directory renames or different mount points.
4. **R*-Tree Spatial Index Geometric Correctness & Node Lifecycle Hardening**: Upgraded intersection tests to inclusive bounds ($\le$), added handling for zero-area degenerate boxes (point strokes), tightened parent bounds correctly during deep recursive splits, and implemented clean root collapsing when elements are deleted and re-inserted.
5. **Atomic SQLite Transaction Rollback Protection**: Project saves now enforce strict transactional rollback (`ROLLBACK;`) if any statement fails during differential upserts, preventing partial or corrupted database commits.
6. **Zero-Length Click Stroke Clutter Prevention**: Accidental single clicks or micro-displacement taps (< 1.0 px) on both the PDF document reader and the canvas inking surface are filtered out, preventing stray dot strokes or unintentional connector line conversions.
7. **Refined Keyboard Shortcuts & Accelerators**: Unified `Delete` and `Backspace` handling for selected cards and connector lines, `Ctrl+M` persistent minimap toggle, `Alt+1`..`Alt+6` and `F1`..`F6` tool accelerators, and `Esc` tool reset.

---

### 🌟 What's New & Fixed in v1.1.4

#### 1. Spacebar Minimap Peeking & Synchronized Viewport Navigation
- **Temporary Radar Peeking via `Spacebar`**:
  - In addition to canvas panning (`Space + Drag`), pressing and holding `Spacebar` now activates a live peek of the minimap navigation widget (`isSpacePeekingMinimap = true`).
  - Upon releasing `Spacebar`, the minimap gracefully dismisses if persistent minimap mode was turned off.
- **Bi-Directional State Synchronization**:
  - The top toolbar minimap button and `Ctrl+M` keyboard shortcut stay strictly in sync with `WorkspaceView` state via `setOnMinimapVisibilityChanged`.
  - Added re-entrancy protection (`m_updatingMinimapState`) preventing cascading toggle signal loops.
- **High-Performance Minimap Rendering**:
  - Optimized minimap rendering loop to filter out micro canvas ink strokes (`CanvasStrokeNode`), drastically reducing Cairo drawing overhead when hundreds of handwriting strokes are present while maintaining full visibility for cards, decks, and structural groups.

#### 2. Clipboard Image Pasting (`Ctrl+V`) & Raster Drag-and-Drop
- **Direct Clipboard Paste (`Ctrl+V`)**:
  - Paste screenshots or copied images directly from the OS clipboard into the infinite canvas.
  - Automatically discriminates between text and image clipboard payloads, preserving normal text entry if a text entry box is focused.
  - Generates unique timestamped PNG assets in `assets/images/` inside the project bundle (or session assets for unsaved workspaces).
- **Drag-and-Drop Image Files**:
  - Dragging PNG, JPEG, JPG, BMP, WEBP, GIF, TIFF, or ICO files into the workspace window seamlessly ingests and embeds them into the workspace.
- **Aspect-Ratio-Preserving Geometry**:
  - Image cards calculate optimal viewport dimensions based on native image aspect ratio:
    $$W = 320\text{ px}, \quad H = \text{clamp}\left(\text{header} + \text{padding} + (W - 32) \cdot \frac{\text{imgH}}{\text{imgW}},\, 120,\, 800\right)$$
  - Placed directly at current viewport center or drop coordinates, and pushed onto the undo stack via `InsertNodeCommand`.
- **High-Fidelity Raster Rendering**:
  - `WorkspaceRenderer` includes dedicated raster image caching (`s_imgSurfaceCache`) and header title formatting, cleanly rendering image crops alongside PDF page excerpts.

#### 3. External Document Persistence & Resolution Fallback Engine
- **Persistent External PDF Paths**:
  - Added `external_path TEXT` column to `documents` SQLite table schema in `ProjectStore`.
  - When referencing external documents without copying them into the bundle, FluidCore permanently remembers the source filesystem location.
- **Companion Path Tracking in `DocumentPane`**:
  - Decoupled document viewing path from annotation storage path via `m_companionPath`, allowing annotations to be loaded and saved to dedicated companion files or directly to external PDFs.
- **Multi-Tiered Document Resolution**:
  - Enhanced `PdfDocumentService::resolveEntryLocked` with multi-strategy resolution: exact docId match, normalized canonical path matching, filename-only matching, filesystem existence fallback, and single-document project fallback.
  - Robust URI encoding and file path normalization prevent malformed file URI errors on Windows drive letters and Linux paths.

#### 4. R*-Tree Spatial Index Geometric Robustness
- **Inclusive Boundary Overlaps**:
  - Changed box intersection from strict inequality (`<`) to inclusive inequality (`<=`):
    $$\text{intersects}(A, B) \iff A.x \le B.x + B.w \;\land\; B.x \le A.x + A.w \;\land\; A.y \le B.y + B.h \;\land\; B.y \le A.y + A.h$$
  - Resolves edge-touching query misses where cards or strokes sharing boundary coordinates were erroneously excluded from viewport queries.
- **Degenerate Point Rectangles**:
  - Fixed `unionOf` to allow zero-width and zero-height bounding boxes (`a.w < 0 || a.h < 0`), properly supporting point hits and zero-size ink stubs.
- **Deep Split Bounds Propagation**:
  - Ensured `tightenUp(nodeIdx)` is systematically invoked after multi-level node splits, guaranteeing that parent bounding boxes strictly enclose all child nodes up to the root.
- **Root Collapse & Re-Insertion Safety**:
  - Implemented `collapseRootIfNeeded()`, properly recycling empty roots and collapsing single-child interior roots when nodes are deleted or moved, preventing tree corruption when clearing a canvas and inserting new nodes.

#### 5. Project Persistence & Transaction Integrity
- **Atomic Rollback on SQLite Statement Failure**:
  - Introduced `executeOrRollback` in `ProjectStore::saveProject`.
  - Any error encountered during metadata update, document registration, node upsert, edge upsert, or deletion immediately executes `ROLLBACK;`, preventing partial database writes.
- **Project Dirty Tracking**:
  - `UndoStack` change listeners automatically mark the project dirty (`viewCtx->isProjectDirty = true`) when uncommitted actions exist, preventing accidental lost work.

#### 6. Micro-Tap Ink Clutter Prevention & Usability Polish
- **Accidental Click / Micro-Tap Filtering**:
  - Ink strokes with $\le 1$ sample or displacement $\|\Delta \vec{p}\| < 1.0\text{ px}$ are filtered out in both `InkOverlay` and `WorkspaceView`.
  - Eliminates accidental dot marks and false-positive connector stroke conversions when clicking to focus.
- **Unified Deletion**:
  - Pressing `Delete` or `Backspace` cleanly removes whichever entity is selected (card, stack, or relational link connector) via `deleteSelected()`.
- **Keyboard Accelerators**:
  - `Ctrl + M`: Toggle minimap
  - `Ctrl + V`: Paste image from clipboard
  - `Alt + 1` .. `Alt + 6` & `F1` .. `F6`: Direct tool selection (Pen, Highlighter, Eraser, Select, Crop, Connector)
  - `Esc`: Cancel active interaction, clear text/crop selections, and return tool to Select

---

### 📦 Downloads & Binaries

| Platform | Package | Format | File Name | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Windows** | **Native Installer** | `.exe` | `FluidCore-Setup-x64.exe` | **Recommended for Windows.** Inno Setup 64-bit installer with Start Menu integration, desktop shortcut, uninstaller, and `.ltproj` project associations. |
| **Windows** | **Portable Zip** | `.zip` | `fluidcore-windows-x64.zip` | Standalone portable archive containing all required UCRT64 runtime DLLs, GLib schemas, GDK-Pixbuf loaders, and Adwaita icons. Run anywhere without admin privileges. |
| **Linux** | **AppImage Bundle** | `.AppImage` | `FluidCore-1.1.4-x86_64.AppImage` | **Recommended for Linux.** Standalone portable executable with bundled dependencies and Wayland/X11 support. Run anywhere (`chmod +x`). |
| **Linux** | **Debian Package** | `.deb` | `fluidcore_1.1.4_amd64.deb` | Native Debian/Ubuntu package (`apt install ./fluidcore_1.1.4_amd64.deb`) with system dependency management and FreeDesktop menu entry. |
| **Linux** | **Flatpak Manifest** | `.yml` | `ops/flatpak/org.fluidcore.platform.yml` | Sandboxed Flathub distribution manifest with zero-network isolation (`--unshare=network`). |

---

### 🧪 Verification & Test Suite Summary

- **100% CTest Suite Passing**: All **39/39** test suites passing with zero regressions:
  - `RTreeIndexTest`: Validates inclusive boundary intersection, degenerate point queries, deep-split recursive bounds tightening, and empty root collapse/re-insertion.
  - `ProjectStoreTest`: Validates `external_path` round-trip persistence, idempotent schema upgrades, and transactional rollback on failures.
  - `WorkspaceInteractionTest`: Validates `isSpacePeekingMinimap` state transitions and minimap geometry calculations.
  - `ExcerptTileCacheTest`: Validates crop cache invalidation and multithreaded tile rasterization.
  - `StrokeStabilizerTest`: Validates curvature-adaptive deadzone scaling and centripetal Bézier smoothing.
  - `PopplerMalformedRegressionTest` & `PopplerConcurrencyTest`: Confirms air-tight worker lifecycle and memory bounds.
- **Architectural Invariants Gate**: Passed (`ops/scripts/check_invariants.py` — strict zero-network boundary, clean GUI/core decoupling).
