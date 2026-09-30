# Bug Report: Excerpt Card Crop Aspect Distortion & Placeholder Flicker on Zoom

**Status:** Fixed & Shipped  
**Severity:** High (visual correctness & perceived latency on infinite synthesis canvas)  
**Component:** `src/app/workspace/` (rendering & interaction), `src/app/services/ExcerptTileCache.*`, `src/app/services/TileSizing.h`, `src/app/services/PdfDocumentService.cpp`  
**Date:** 2026-09-30  

---

## Summary

Excerpt cards on the infinite synthesis canvas exhibited two coupled defects during viewport interactions:
1. **Aspect Distortion on Zoom**: Diagram crops were progressively squashed horizontally and stretched vertically as zoom increased from 118% to 200% (`kMaxZoom = 2.0`), reaching ~17% distortion. Additionally, an intrinsic ~1.15% baseline aspect skew existed across all zoom levels.
2. **Continuous Placeholder Flicker**: Cards repeatedly flashed the dashed pale blue-grey "Visual Diagram Crop" placeholder during interactive zooming and immediately following the 20-second idle memory trim.

---

## Detailed Root Cause Analysis

### Defect A: Aspect Ratio Distortion
1. **Independent Per-Axis Clamping**: In `renderCropSync` and `requestCropAsync`, target dimensions were computed via independent clamps: `clamp(w - 20, 16, 1536) x clamp(h - 36, 16, 1536)`. At 200% zoom on a large card (e.g. 1832x1390 px requested), width clamped to 1536 while height remained 1390, distorting aspect ratio.
2. **Non-Uniform Raster Transform**: `PdfDocumentService::renderBackgroundCrop` applied `cairo_scale(cr, targetW / cropW, targetH / cropH)`, baking the distorted aspect ratio directly into the rendered Cairo surface.
3. **Three-Way Geometry Mismatch**:
   - `CardLayoutEngine` reserved 28x46 pt chrome (`imgW + 28`, `imgH + 46`).
   - `WorkspaceRenderer` drew 32x40 pt chrome (16 anchor + 8 margin left/right, 28 header + 6 margin top/bottom).
   - `WorkspaceView` requested 8x10 pt offsets (`cardW - 20`, `cardH - 36`).
   This three-way discrepancy produced a baseline ~1.15% skew before zooming began.

### Defect B: Placeholder Flicker and Budget Thrashing
1. **Missing Byte Ceiling in Production Path**: While test harnesses assumed a 4 MiB per-tile ceiling, production code allowed oversized tiles ($1832 \to 1536 \times 1390 \times 4 = 8,540,160\,\text{B} \approx 8.14\,\text{MiB}$).
2. **Budget Thrashing**: The 24 MiB working-set budget for `Slice::ExcerptTiles` was exhausted by just three 8.14 MiB tiles. At idle (6 MiB budget), a single Retina tile exceeded the entire budget, forcing immediate eviction of on-screen cards.
3. **Blind Eviction of Active Cards**: `evict()` and `trimToBytes()` blindly popped LRU nodes without verifying whether they were visible on-screen.
4. **Sibling Tier Purging**: `evictSiblingTiers()` dropped all lower-resolution tiers upon inserting a higher tier, leaving zero fallback surfaces during zoom transitions or resize commits.

---

## Resolution & Implementation

1. **Uniform Fit-Inside Sizing Policy (`TileSizing.h`)**:
   - Replaced independent clamps with uniform scaling: $s = \min(T_w / W_{\text{crop}}, T_h / H_{\text{crop}})$.
   - Added successive scale reduction to strictly enforce the 1536 px dimension limit and 4 MiB byte ceiling ($1024 \times 1024 \times 4$).
   - Post-rounding overflow guard decrements the major axis if $w \times h \times 4 > 4\,\text{MiB}$.
   - Lower bound floored at 1 px (preserving extreme sliver aspect ratios such as 5x700 pt).
2. **Canonical Geometry Engine (`CardLayoutEngine.h`, `.cpp`)**:
   - Standardized canonical chrome constants: `kTotalChromeWidth = 32.0`, `kTotalChromeHeight = 40.0`.
   - Exposed authoritative `cardImageBodyWorldRect()` and `cardImageBodyScreenRect()`.
3. **Viewport-Aware Crop Pinning (`ExcerptTileCache.h`, `.cpp`)**:
   - Tracked visible on-screen cards via `CropIdentity` set updated per frame.
   - `evict()` and `trimToBytes()` skip pinned cards to eliminate flicker.
4. **Deterministic Bounded Retention on Resize (`invalidateCrop`)**:
   - Added explicit `LodTier purgeTier` parameter passed from `WorkspaceView`.
   - Purges `purgeTier` and retains the single lowest resident tier $\ne \text{purgeTier}$ as fallback, preventing first-writer-wins lock-in while avoiding pseudo-Overview tier forgery.
5. **Lock-Free Worker Cancellation (`cancelToken`)**:
   - Added `std::shared_ptr<std::atomic<bool>> cancelToken` to `AsyncRenderTask` and `InFlightEntry`.
   - Workers bail before acquiring `globalPopplerMutex()` if cancelled, saving 20–50 ms of CPU per obsolete raster.
   - Telemetry tracks `m_skippedRasters` (raster avoided before Poppler) and `m_droppedRasters` (rendered then discarded).
6. **Zero-Allocation LRU Scans**:
   - Embedded `CropIdentity cropId` into `CacheNode`, eliminating heap allocations during LRU traversals.

---

## Verification

- **Automated Test Suite**:
  - `ExcerptTileCacheTest` (23 tests, 100% pass): verified dimension ceiling, 4 MiB byte ceiling, aspect ratio preservation ($\le 0.06\%$ error), sliver crop preservation, deterministic stale-tier cancellation with latch (`skippedRasters >= 1, droppedRasters == 0`), and resize fallback retention.
  - `CropDragCrashTest` (100% pass): verified stable drag and drop without memory corruption or use-after-free.
- **Interactive Verification**:
  - Zoom transitions (118% $\to$ 200% $\to$ 118%) with 4 excerpt cards show zero aspect distortion and zero placeholder flicker.
  - Card resize re-renders accurately at new dimensions with zero placeholder flashes.
