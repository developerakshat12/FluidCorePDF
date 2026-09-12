# FluidCore v1.1.3 — Excerpt Card Resizing, Smooth Bézier Pen Inking, Layer Stacking, & Comprehensive Memory Remediation

FluidCore **v1.1.3** delivers high-impact slice-of-life usability enhancements, inking rendering fidelity upgrades, and deep architectural memory leak remediations:

1. **Interactive Excerpt Card Resizing**: Resize text and visual crop excerpt cards with live high-resolution rasterization, strict aspect-ratio locking for visual crops, and full Undo/Redo integration.
2. **Silky-Smooth Canvas Pen Inking**: Eliminates low-pixel faceting, jagged lines, and polygonal clipping on tight circles and loops via curvature-adaptive deadzone scaling and centripetal Catmull–Rom Bézier spline interpolation.
3. **Highlighter Layer Stacking**: Highlighter strokes always render cleanly beneath solid pen strokes across both the PDF Document Reader and the Infinite Spatial Workspace canvas.
4. **PDF Page Position Persistence**: Projects now remember and automatically restore the active PDF reading page across saves and reloads, backed by an idempotent SQLite schema migration.
5. **Comprehensive Memory Leak Remediation**: Root-cause resolution of MinGW `winpthread` mutex retention via native Win32 `CRITICAL_SECTION` patching, ephemeral background search workers, and `mimalloc` heap slack reclamation.
6. **Linux Distribution Packaging & ABI Portability Fix**: Re-targeted Linux CI and release packagers to Ubuntu 22.04 LTS (GLIBC 2.35), statically linked C++ runtime (`-static-libgcc -static-libstdc++`), and refined library bundling in AppImage/.deb to eliminate `GLIBC_2.38` and `GLIBCXX_3.4.31` launch errors.

---

### 🌟 What's New in v1.1.3

#### 1. Interactive Excerpt Card Resizing
- **Intuitive Visual Grab Handles**:
  - Hovering over or selecting any excerpt card reveals a 14 px circular grab handle at the bottom-right corner.
  - Contextual cursor updates to `se-resize` (`GDK_BOTTOM_RIGHT_CORNER`) upon hover.
- **Strict Aspect-Ratio Locking for Visual Crop Cards**:
  - Image crop cards automatically lock and preserve their original $(W:H)$ aspect ratio:
    $$\text{scale} = \max\left(\frac{W_0 + \Delta x}{W_0},\, \frac{H_0 + \Delta y}{H_0}\right), \quad W = \text{clamp}(W_0 \cdot \text{scale}, 160, 1600), \quad H = \frac{W}{AR}$$
  - As visual cards enlarge or shrink, the excerpt image dynamically scales to fit the card geometry without any distortion or letterboxing.
- **Full Undo / Redo Support (`Ctrl+Z` / `Ctrl+Y`)**:
  - Backed by the newly implemented `ResizeNodeCommand` in `libfluidcore`.
  - Seamlessly pushes commands to the undo stack upon mouse release, with automatic R-Tree spatial index recalculation.
- **Dynamic Tile Cache Invalidation**:
  - Upon completing a card resize, the excerpt tile cache automatically invalidates and re-renders the underlying PDF region at the new scale, ensuring vector-sharp clarity even at large expansions.

#### 2. Silky-Smooth Canvas Pen Inking & Anti-Faceting Engine
- **Curvature-Adaptive Deadzone Filtering**:
  - Previous fixed-distance stabilization deadzones ($1.5$–$2.0$ px) truncated sharp directional changes, turning small loops and circles into faceted polygons at low pixel widths (1–2 px).
  - The new `StrokeStabilizer` measures the turning angle between successive stroke vectors:
    $$\cos \theta = \frac{\Delta \vec{v}_{i} \cdot \Delta \vec{v}_{i-1}}{\|\Delta \vec{v}_{i}\| \, \|\Delta \vec{v}_{i-1}\|}$$
  - When a sharp turn or circle is detected ($\cos \theta < 0.95$), the deadzone automatically scales down to **$0.15$ px**, preserving tight curves and circular handwriting with zero polygonal flattening.
- **Centripetal Catmull–Rom Bézier Splines**:
  - Replaced linear segment chaining with centripetal Catmull–Rom cubic Bézier splining across both committed canvas strokes and live in-flight wet ink.
  - Live wet ink and committed nodes evaluate 4-step sub-segment Bézier curves with smooth pressure interpolation via `evalCubicBezier`, eliminating visual "popping" or snapping upon pen-up.

#### 3. True Layer Stacking: Highlighter Sits Beneath Pen
- **PDF Document Overlay (`InkOverlay`)**:
  - Split rendering into a two-pass compositor: Pass 1 renders all highlighter strokes in semi-transparent mode (`CAIRO_OPERATOR_OVER`); Pass 2 renders solid ink pens directly on top.
- **Infinite Spatial Workspace Canvas (`WorkspaceRenderer`)**:
  - Re-architected canvas layer stacking order:
    1. Excerpt Cards, Stacks, and Structural Elements
    2. Committed Highlighter Strokes
    3. Active Wet Inking Highlighter Stroke
    4. Committed Pen Strokes
    5. Active Wet Inking Pen Stroke
    6. Selection Rings, Grab Handles, and Connectors
  - Highlighting over existing writing or sketching never dims, occludes, or muddies pen strokes.

#### 4. PDF Reading Position Persistence Across Project Reloads
- **Zero-Friction Context Restoration**:
  - Saving a project (`Ctrl+S`) records the exact PDF page currently visible in the document reader viewport.
  - Reopening the `.ltproj` project bundle automatically scrolls the document reader directly to that page once dimensions and layout are computed.
- **Idempotent SQLite Schema Migration**:
  - Added `last_viewed_page INTEGER NOT NULL DEFAULT 0` to the `documents` table in `ProjectStore`.
  - Uses `PRAGMA table_info(documents)` inspection prior to running `ALTER TABLE`, guaranteeing seamless, idempotent upgrades of existing project databases without throwing duplicate column errors.

#### 5. Deep Memory Leak Remediation & Architecture Hardening
- **Native Win32 CriticalSection Remediations**:
  - Diagnosed and resolved a subtle memory retention bug in MinGW-w64's `winpthread` library, where `std::mutex` allocations bypassed standard CRT cleanup upon thread termination.
  - Applied native Win32 `CRITICAL_SECTION` fallbacks in `ops/patches/poppler_win32_critical_section.patch` for rock-solid concurrency stability.
- **Ephemeral Background Search Document Lifecycle**:
  - Document search workers now utilize an ephemeral `PopplerDocument` lifecycle and scheduled worker thread recycling (every 8 searches), releasing page descriptor caches immediately upon search completion.
- **`mimalloc` Hardened Heap Management**:
  - Linked `mimalloc` across `fluidcore_app` with `PurgeDelay=0` and active decommit flags to aggressively return unmapped virtual memory slack directly to Windows.
  - Added internal live memory telemetry (`MemoryTelemetry.h`) and telemetry monitoring scripts in `debug/scripts/`.

#### 6. Linux Distribution Packaging & ABI Portability Hardening
- **Universal GLIBC 2.35+ Compatibility**:
  - Re-targeted GitHub Actions release and packaging runners from `ubuntu-24.04` to `ubuntu-22.04`.
  - Releases are now built against GLIBC 2.35, resolving runtime failures (`version GLIBC_2.38 not found`) on Ubuntu 22.04 LTS, Debian 12, Linux Mint 21, and other enterprise distributions, while maintaining forward compatibility with Ubuntu 24.04+.
- **Static C++ Runtime Linking (`-static-libgcc -static-libstdc++`)**:
  - Statically linked `libstdc++` and `libgcc` into `fluidcore_app` on Linux, eliminating `GLIBCXX_3.4.xx` runtime version mismatches.
- **AppImage & Debian Package Refinements**:
  - Updated `package-appimage.sh` excludelist to prevent bundling `libstdc++`/`libgcc_s` which could shadow newer host GPU display drivers.
  - Enhanced `package-deb.sh` dependencies with `libpoppler-glib8 (>= 20.0) | libpoppler-glib8t64` for seamless installation across both Ubuntu 22.04 and Ubuntu 24.04 (t64 transition).

---

### 📦 Downloads & Binaries

| Platform | Package | Format | File Name | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Windows** | **Native Installer** | `.exe` | `FluidCore-Setup-x64.exe` | **Recommended for Windows.** Inno Setup 64-bit installer with Start Menu integration, desktop shortcut, uninstaller, and `.ltproj` project associations. |
| **Windows** | **Portable Zip** | `.zip` | `fluidcore-windows-x64.zip` | Standalone portable archive containing all required UCRT64 runtime DLLs, GLib schemas, GDK-Pixbuf loaders, and Adwaita icons. Run anywhere without admin privileges. |
| **Linux** | **Debian Package** | `.deb` | `fluidcore_1.1.3_amd64.deb` | Native Debian/Ubuntu package (`apt install ./fluidcore_1.1.3_amd64.deb`) with system dependency management and FreeDesktop menu entry. |
| **Linux** | **AppImage Bundle** | `.AppImage` | `FluidCore-1.1.3-x86_64.AppImage` | **Recommended for Linux.** Standalone portable executable with bundled dependencies and Wayland/X11 support. Run anywhere (`chmod +x`). |

---

### 🧪 Verification & Test Suite Summary
- **100% CTest Suite Passing**: All **39/39** unit and integration test suites pass with zero failures:
  - `ProjectStoreTest`: Validates `lastViewedPage` persistence round-trip and idempotent schema migration.
  - `UndoStackTest`: Validates `ResizeNodeCommand` execution, undo, redo, and spatial R-Tree index bounding box updates.
  - `StrokeStabilizerTest`: Validates adaptive curvature deadzone scaling on high-frequency directional turns.
  - `PopplerMalformedRegressionTest` & `PopplerConcurrencyTest`: Validates thread safety and memory bounds.
- **Invariants Gate**: `python ops/scripts/check_invariants.py` passed (zero runtime network calls, strict `libfluidcore` engine boundary).
- **Code Style Compliance**: 100% compliant with `clang-format --dry-run --Werror`.
