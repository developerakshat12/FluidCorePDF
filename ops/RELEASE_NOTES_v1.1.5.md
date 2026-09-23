# FluidCore v1.1.5 — Linux Touchpad Smooth Scrolling, Scrollbar Drag Performance, & Persistence Hardening

FluidCore **v1.1.5** is a dedicated performance and bug fix release focused on reading fluidity, input responsiveness, and storage reliability across Linux and Windows:

1. **Linux Touchpad Two-Finger Scrolling**: Full support for continuous `GDK_SCROLL_SMOOTH` touchpad gestures under X11/libinput and Wayland. Two-finger scrolling now uses native GTK kinetic acceleration ($\Delta y \cdot \text{pageSize}^{2/3}$), while discrete mouse wheel scrolling continues to operate with native physical precision.
2. **Buttery-Smooth Scrollbar Dragging**: Completely eliminated scrollbar stutter and UI lag when rapidly browsing documents. Active viewport pinning preserves on-screen pages in memory without eviction thrashing, while interactive drag throttling keeps the GTK main loop executing at a silky 60 FPS.
3. **Viewport Pinning Cache Eviction Fix**: Separated partial damage clip rectangles from the active reading viewport in `DocumentPane::draw()`. Visible pages are now protected from premature cache eviction during small scroll increments.
4. **Concurrency & Crop Tile Cache Stability**: Hardened background crop rasterization in `PdfDocumentService` and excerpt card rendering in `ExcerptTileCache`.
5. **Project Store Legacy Schema Migration Hardening**: Verified and hardened automated SQLite migrations for legacy projects containing external PDF paths (`external_path`) and last viewed pages (`last_viewed_page`).
6. **Air-Gapped Zero-Telemetry Verification**: Verified that all 39 CTest test suites pass with zero network calls and strict memory limits ($\le 1.2\text{ GB}$).

---

### 🌟 What's New & Fixed in v1.1.5

#### 1. Linux Touchpad Two-Finger Scrolling & Smooth Deltas
- **Smooth Scroll Event Mask Registration**:
  - Registered `GDK_SMOOTH_SCROLL_MASK | GDK_SCROLL_MASK` on all viewport and overlay widgets: `m_scroller` (`GtkScrolledWindow`), `m_overlay` (`GtkOverlay`), `m_area` (`GtkDrawingArea`), and `m_inkOverlay->widget()`.
  - Connected `scroll-event` on `m_inkOverlay->widget()` to ensure gestures over active inking canvases are properly intercepted and processed.
- **Continuous Kinetic Scrolling**:
  - In `DocumentPane::onScroll()`, handled `GDK_SCROLL_SMOOTH` via `gdk_event_get_scroll_deltas`.
  - Updated vertical and horizontal adjustments (`vadj` / `hadj`) using standard GTK smooth kinetics:
    $$\Delta = \text{deltaY} \cdot \text{pageSize}^{2/3}$$
  - Preserved discrete mouse wheel fallback (`return FALSE;`) so `GtkScrolledWindow` continues handling physical scroll wheels natively.
- **Clamped Trackpad Zoom & Squeeze Gestures**:
  - Clamped sub-pixel trackpad deltas during Ctrl+scroll zoom and Shift+scroll squeeze to $[-2.5, 2.5]$, preventing erratic zoom jumps when swiping on high-precision trackpads.

#### 2. Elimination of Scrollbar Dragging Lag & Cache Thrashing
- **True Active Viewport Pinning**:
  - In `DocumentPane::draw()`, separated the Cairo damage rectangle (`clip`) from the active viewport (`[viewYStart, viewYEnd]`).
  - All pages currently overlapping the on-screen viewport (plus prefetch margin) are pinned in `PageTileCache`.
  - Eliminates the severe cache thrashing loop where partial-clip redraws unpinned visible pages, forcing repeated synchronous Poppler rasterization on every scroll tick.
- **Interactive Scrollbar Drag Throttling**:
  - Connected `button-press-event` and `button-release-event` to `gtk_scrolled_window_get_vscrollbar()`, tracking `m_isScrollbarDragging`.
  - Added a 100ms debounce timer on `vadj`'s `value-changed` signal.
  - During rapid scrollbar dragging across uncached pages, `draw()` renders clean white page placeholder rectangles at 60 FPS without synchronously locking `globalPopplerMutex()` or blocking the UI thread.
  - As soon as dragging settles or the mouse button is released, the settled viewport page is immediately rasterized and cached.

#### 3. Storage & Migration Hardening
- **Schema Migration Conformity in `ProjectStoreTest`**:
  - Fixed legacy DDL migration test in `ProjectStoreTest.cpp` to use the canonical `projects` table and `file_path_relative` column name.
  - Ensured seamless rehydration of external PDF document paths (`external_path`) and last reading position across legacy `.ltproj` bundles.

---

### 📦 Platform Binaries & Packaging

- **Windows 11 (Native)**:
  - Inno Setup installer: `FluidCore-Setup-x64.exe` (v1.1.5)
  - Portable zip: `fluidcore-windows-x64.zip` (v1.1.5)
  - Resources: `src/app/fluidcore.rc` version set to `1.1.5.0`
- **Linux (Ubuntu / Debian / Fedora / Arch)**:
  - AppImage: `FluidCore-1.1.5-x86_64.AppImage`
  - Debian package: `fluidcore_1.1.5_amd64.deb`
  - AppStream metainfo: `resources/linux/org.fluidcore.platform.metainfo.xml` updated with v1.1.5 release metadata

---

### 🧪 Verification & Test Results

- **WSL Linux Build**: Clean build with `make -j4` (0 warnings, 0 errors).
- **Windows Ninja Build**: Clean build with `ops\scripts\build-win.ps1` (0 warnings, 0 errors).
- **CTest Suite**: 39/39 tests passed (100% pass rate).
- **Memory & Scalability**: Verified with `ScalabilityBenchmarkTest` ($\le 1.2\text{ GB}$ ceiling preserved).
