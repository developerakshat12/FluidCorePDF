# FluidCore v1.1.8 — Aspect-Correct Crop Tiles & Stable Excerpt Caching

FluidCore **v1.1.8** is an excerpt-card rendering correctness release. Diagram crops dropped onto the infinite synthesis workspace are no longer distorted, and cards no longer flash the "Visual Diagram Crop" placeholder while their real content is already cached. It also cancels raster work for zoom tiers the user has already zoomed past, and collapses four disagreeing copies of the card geometry constants into one definition.

Both defects had a single origin: the crop-tile pipeline derived tile pixel dimensions from *card* geometry and then constrained them with **per-axis, independent** clamps, while the production path never enforced the per-tile byte ceiling that the test path did. The aspect skew was baked into the cached raster at creation time and was therefore irreversible; the oversized tiles made those skewed surfaces big enough that the cache could only hold two, so the rest of the screen had nothing to draw.

1. **Crops Are No Longer Squashed**: A single uniform scale is derived from the authoritative PDF crop box, and every ceiling is applied as a uniform reduction of that one scale. No clamp can stretch one axis independently.
2. **The 4 MiB Tile Ceiling Now Applies in Production**: It was previously enforced only by `renderCropSync`, the synchronous path used by tests and benchmarks. A 200%-zoom card produced an 8.14 MiB tile against a 24 MiB slice.
3. **Cards Stop Flashing the Placeholder**: The cache pins every crop visible in the viewport and retains one lightweight lower-resolution tier, so there is always a real image to blit while a sharper tile renders.
4. **Obsolete Zoom Tiers Are Cancelled Before Rendering**: Crossing a resolution boundary queued a render per visible card at the intermediate tier. Those rasters ran under the global Poppler lock and were then immediately superseded.
5. **One Definition of Card Geometry**: Layout, rendering, and tile requests now share a single body-rect definition instead of four literals that disagreed.
6. **Fixed**: The Windows executable's version resource was not kept in step with the release; v1.1.7 shipped reporting `1.1.6.0` in Windows file properties.

---

## Aspect Distortion

### The reported bug

With the canvas at 200% zoom, crops of 4:3 slide pages (`CS F301-L2-3(Introduction).pdf`, 720×540 pt) rendered visibly narrower than the corresponding figure on the source page, with boxes inside the diagram condensed and text appearing vertically stretched. The skew reached **≈17%** horizontally.

### Root cause

Three independent per-axis decisions stacked:

1. **The requested box came from the card, not the crop.** `drawExcerptCard` requested `bounds().w - 20.0` by `bounds().h - 36.0`. Substituting the layout engine's own formula (`cardW = imgW + 28`, `cardH = imgH + 46`) gives `imgW + 8` by `imgH + 10` — already a skewed aspect, at every zoom including 100%. For the 4:3 card in the report session (`imgW = 450`, `imgH = 337.5`) that is a **−1.15%** baseline error. Small enough to go unnoticed; the same error that the next step amplified.
2. **Each axis was clamped to 1536 px independently.** `requestCropAsync` did `clamp(cardW * lodScale, 16, 1536)` and `clamp(cardH * lodScale, 16, 1536)` separately.
3. **The raster transform was per-axis.** `renderBackgroundCrop` called `cairo_scale(cr, targetW / cropW, targetH / cropH)`.

For the reported card (`cardW = 478`, `cardH = 383.5`, so a base request of 458×347.5), the `Retina` tier asked for 1832×1390. The width saturated at 1536; the height did not. The result was a 1536×1390 tile carrying an aspect of 1.1050 instead of 1.3333 — **−17.12%** — with the squeeze already rasterized into the surface.

This was unrecoverable. The blit in `drawExcerptCard` is deliberately uniform (`scale = min(bodyW/surfW, bodyH/surfH)`), which is correct, and it faithfully reproduces whatever aspect the tile already had. Re-blitting at a different zoom, tier, or card size could not undo it, so the skewed surface was served for the lifetime of the cache entry.

### The fix

`renderBackgroundCrop` is the only place that calls `poppler_page_get_size`, so it is the only place that can know the crop's true point dimensions. All sizing now happens there, and the caller's two numbers are treated as a **bounding box constraint** rather than a pixel size:

```cpp
// src/app/services/TileSizing.h
double s = std::min(static_cast<double>(targetW) / cropW,
                    static_cast<double>(targetH) / cropH);
s = std::min(s, static_cast<double>(kMaxTileDimension) / cropW);
s = std::min(s, static_cast<double>(kMaxTileDimension) / cropH);
```

There is deliberately **no `min(1.0, …)`** on that expression. Capping the scale at 1 px/pt would pin every tile to 72 DPI and destroy Retina supersampling on high-DPI displays; the whole point of the tier ladder is to render above 1 px/pt. The byte ceiling is then a further uniform reduction:

```cpp
const double maxPixelArea = static_cast<double>(kMaxTileBytes) / 4.0;
const double pixelArea = (cropW * s) * (cropH * s);
if (pixelArea > maxPixelArea) {
    s *= std::sqrt(maxPixelArea / pixelArea);
}
```

Two details that are easy to get wrong and were both handled:

- **The floor is 1 px, not 16 px.** A per-axis minimum re-introduces exactly the defect this change removes. On a 5×700 pt sliver crop, a 16 px floor yields a 16×338 tile — a 6.6× distortion — where a 1 px floor preserves the true ratio.
- **The byte ceiling is re-checked after rounding.** Rounding `cropW * s` and `cropH * s` to integers can push the product back over the limit (a 1449×724 tile is 4,196,304 B, over the 4,194,304 B ceiling). A guard decrements the longer axis until the tile fits.

For the reported card, the `Retina` tile is now 1155×866 = 3.81 MiB, down from 8.14 MiB, at an aspect of 1.3333.

`renderCropSync` was routed through the same policy, so the test and benchmark paths can no longer diverge from production.

---

## Placeholder Flicker

### The reported bug

Cards repeatedly flashed the dashed "Visual Diagram Crop" placeholder while their content was already cached, on every zoom gesture, and worst immediately after the 20-second idle trim.

### Root cause

The 4 MiB per-tile ceiling lived in `renderCropSync` only. The asynchronous path — the one the renderer actually uses — had no byte ceiling at all, so a `Retina` tile was 8,540,160 B against a `Slice::ExcerptTiles` budget of 24 MiB working / 6 MiB idle. Three such tiles did not fit; at idle, a single one exceeded the entire budget. With three or four cards on screen, at least one was guaranteed to be missing at any instant.

That alone would make the placeholder *likely*. Three mechanisms made it *unavoidable*:

1. **`evict()` was blind.** It popped the LRU tail with no knowledge of what was on screen, so it took visible cards first.
2. **`evictSiblingTiers()` removed the safety net.** On inserting a higher tier it purged every other tier of the same crop. `getBestAvailableSurface()` falls back to another resident tier — and the eviction policy had just guaranteed there wasn't one. A card could reach *zero* resident surfaces, at which point the placeholder is the only correct output.
3. **The idle trim cleared everything.** `main.cpp` calls both `setMaxBytes(6 MiB)` and `trimToBytes(6 MiB)`. `trimToBytes` had no pinning either, so the working set was dropped wholesale and the next repaint began blank for every visible card.

The result was a closed loop: card 1's high tier completes → its fallback is purged → card 2's render completes → card 1 is evicted → card 1 shows the placeholder → card 1 redraws and re-dispatches. Repeating at the render-completion rate.

### The fix

**Viewport pinning.** `WorkspaceRenderer::drawExcerptCard` records a `CropIdentity` — `(docId, pageNo, xNorm, yNorm, wNorm, hNorm)`, deliberately tier-independent — for every visible crop, and commits the set at frame end. `evict()` and `trimToBytes()` search for an unpinned entry and halt if every resident tile belongs to an on-screen card.

Pinning by *crop identity* rather than by exact cache key is load-bearing. A `CropCacheKey` includes the tier, so pinning it would pin nothing at exactly the moment of a tier boundary — the instant the flicker happens — while leaving evictable the one lower tier that was preventing the placeholder.

The pin set is collected inside `drawExcerptCard` rather than at the top of the paint pass, because stacked excerpt cards are drawn via `drawCardStack` → `drawExcerptCard` and their children are not returned by the model's `visibleIn()` query. Collecting inside the function covers both paths.

**Fallback retention.** `evictSiblingTiers()` now keeps one `Overview` or `Standard` tile per crop, so `getBestAvailableSurface()` always resolves during a zoom transition. This is what converts the hard oscillation into a brief softening.

With the byte ceiling now enforced in production, the `Retina` tile dropped from 8.14 MiB to 3.81 MiB, which alone moves working-budget residency from two cards to six.

---

## Obsolete Zoom Tiers Were Rasterized Before Being Discarded

Crossing a resolution boundary (`computeLodTierFromZoom` has hard edges at 0.35 / 0.85 / 1.75) invalidates every visible card's key at once. Each card then queues a render at the tier it is leaving. Those tasks sat in a two-worker `GThreadPool` and were picked up regardless of whether they were still wanted.

Cancellation is performed **at dispatch**, on the main thread, inside `requestCropAsync`: any in-flight request for the same crop at a different tier has its per-task `cancelToken` set, and its in-flight bookkeeping unmapped synchronously. The worker checks that token immediately before `renderBackgroundCrop` and returns without touching Poppler.

Two placement details decide whether this is correct or a memory leak:

- **The token is `std::shared_ptr<std::atomic<bool>>`**, mirroring the existing `aliveToken`. `onRenderCompletedIdle` is the only place that unmaps in-flight bookkeeping, so a worker that bailed early and posted no result would otherwise strand its key in `m_inFlightKeys` forever, and `requestCropAsync` would return 0 for that key for the rest of the process. Cancelling at dispatch means the main thread has already done the unmapping, so an early bail has nothing left to clean up.
- **`m_cancelledRequestIds` was deleted rather than extended.** An earlier revision had the worker consult that set. It is main-thread state, so reading it from a worker is a data race, and a bailing worker never posted the result whose completion would have erased its entry. The `cancelToken` covers every case the set did — including the narrow race where a worker was already past the check — so the set was redundant and was removed rather than guarded.

Telemetry distinguishes the two outcomes, because they mean opposite things: `skippedRasters` counts rasters **avoided** before Poppler was entered; `droppedRasters` counts rasters **completed and then discarded** by a cancellation that arrived too late. Both are reported by `dumpStats` and are observable under `FLUIDCORE_LOG_TELEMETRY=1`.

---

## Card Geometry Was Defined Four Times, and None of Them Agreed

| Consumer | Horizontal | Vertical |
| :--- | :--- | :--- |
| Layout engine card size | `imgW + 28` | `imgH + 46` |
| Draw-path tile request | `cardW - 20` → `imgW + 8` | `cardH - 36` → `imgH + 10` |
| Zoom-settle tile request | `cardW - 16` → `imgW + 12` | `cardH - 40` → `imgH + 6` |
| Resize-commit tile request | `cardW - 16` → `imgW + 12` | `cardH - 40` → `imgH + 6` |
| Rendered body rect | `anchorW + 8`, 16 gutter | `headerH + 6`, 12 gutter |

Three different numbers for the same quantity. The draw path and the zoom-settle path skewed in *opposite* directions, and the layout engine reserved 46 pt of vertical chrome while the renderer drew 40. This is the source of the −1.15% baseline skew described above.

`CardLayoutEngine` now owns the constants (`kCardAnchorWidth`, `kCardHeaderHeight`, `kCardMarginLeft/Right/Top/Bottom`, and the derived `kTotalChromeWidth` / `kTotalChromeHeight`) and exposes two helpers: `cardImageBodyWorldRect()` for tile requests and `cardImageBodyScreenRect()` for the renderer. `computeExcerptCardDimensions`, the draw path, the zoom-settle path, and the resize path all consume the same definition, including the renderer's short-card `sh * 0.35` header clamp.

One consequence worth stating: because the cache key does not encode the requested pixel size, a card resized to a *larger* box at the same zoom would keep its old tile forever. `WorkspaceView`'s resize handler therefore calls `invalidateCrop(..., purgeTier)` with the tier it is about to re-dispatch, guaranteeing the re-render. `invalidateCrop` retains the single lowest resident tier that is not `purgeTier` as a fallback, under its authentic enum value — an earlier revision re-keyed a full-resolution surface as `Overview` to achieve the same retention, which made `tierCounts` misreport and would have let a 4 MiB surface masquerade as a cheap overview tile at low zoom forever.

---

## Files Changed

| Area | Change |
| :--- | :--- |
| `src/app/services/TileSizing.h` | **New.** `TileSizingPolicy::computeAspectPreservingDimensions` — single uniform scale, 1536 px and 4 MiB ceilings as uniform reductions, post-round byte guard, 1 px floor. Sole owner of the tile constants. |
| `src/app/services/PdfDocumentService.cpp` | Sizing delegated to `TileSizingPolicy`; `cairo_scale(cr, s, s)`. Poppler mutex scope deliberately unchanged. |
| `src/app/services/ExcerptTileCache.{h,cpp}` | Added `CropIdentity` + pinning-aware `evict`/`trimToBytes`; fallback retention in `evictSiblingTiers`; stale-tier cancellation at dispatch with per-task `cancelToken`; `invalidateCrop(..., purgeTier)`; `CacheNode::cropId`; `skippedRasters`/`droppedRasters` in `dumpStats`. Removed `m_cancelledRequestIds` and the duplicated tile constants. |
| `src/libfluidcore/workspace/CardLayoutEngine.{h,cpp}` | Canonical chrome constants; `cardImageBodyWorldRect` / `cardImageBodyScreenRect`. |
| `src/app/workspace/WorkspaceRenderer.cpp` | Uses the canonical body rect; publishes the per-frame pin set. |
| `src/app/workspace/WorkspaceView.cpp` | Zoom-settle and resize-commit use the canonical body rect; resize-commit passes `purgeTier`. |
| `src/app/fluidcore.rc` | PE version resource brought in step with the release. |
| `src/app/tests/ExcerptTileCacheTest.cpp` | 8 new suites: aspect preservation across all tiers, Retina dimension/byte ceilings, async byte ceiling, viewport pinning, idle-budget retention, canonical geometry, stale-tier cancellation, resize fallback retention. |
| `src/app/tests/CropDragCrashTest.cpp` | Tile request dimensions derived from the canonical helper. |

**Tests: 42/42 CTest suites pass.** The stale-tier test holds `globalPopplerMutex()` and occupies both pool workers with dummy tasks before dispatching, so the superseded task provably cannot reach Poppler — `skippedRasters >= 1` and `droppedRasters == 0` are real assertions rather than timing luck.

---

## Known Gaps

**End-to-end aspect verification does not run on Windows.** `testRealPdfCropRendering` resolves its fixture from a POSIX path (`/mnt/d/study material/…`) and silently skips on the shipping platform, so the only assertion that exercises `renderBackgroundCrop` end-to-end against a real PDF never executes in CI on Windows. The fit-inside contracts are covered directly through `TileSizingPolicy`, and `testAsyncPathEnforcesPerTileByteCeiling` covers the real asynchronous path against a synthetic PDF, so the policy is not untested — but a genuine PDF regression would not be caught on the primary platform. Fixing this means generating the fixture in-test, as `CropDragCrashTest` and `PdfExportServiceTest` already do.

**Cards saved by earlier versions pick up a few points of letterboxing.** `computeExcerptCardDimensions` moved from `imgW + 28` / `imgH + 46` to `+32` / `+40`, a 6 pt vertical chrome change. Persisted card bounds in existing `.ltproj` bundles were computed with the old constants, so `cardImageBodyWorldRect` returns a body 6 pt taller than those cards were laid out for. There is **no distortion** — the body rect and the tile request both come from the same helper, so the uniform blit simply centres the tile with ~3 pt of padding above and below. Moving or resizing the card recomputes it. No schema migration was added; that is a deliberate trade of a cosmetic, self-healing delta against bundle compatibility risk.

**Resizing a card can still flash the placeholder in one case.** `invalidateCrop` purges `purgeTier` so the re-render is guaranteed, and retains the lowest other resident tier as a fallback. If `purgeTier` was the *only* resident surface, there is nothing to retain and the card shows the placeholder for the duration of one raster. This is accepted deliberately: retaining the current tier instead would re-introduce the size lock-in described above, which is the more durable failure. The affected card is the one being actively dragged.

**The global Poppler mutex is still held for the whole raster.** `renderBackgroundCrop` takes `globalPopplerMutex()` at entry and holds it through `poppler_page_render`, and `m_registryMutex` is held with it. Narrowing that scope is tempting and was considered, but `poppler_document_get_page()` returns a non-owning `GObject*` owned by the document; releasing the lock between there and the render would race `unregisterDocument()` and `clear()`, which destroy the document under that same mutex. Safe narrowing needs a refcounted document entry and a held page reference, which is a larger change than this defect warranted. The stale-tier cancellation reduces how often workers queue on that lock instead.

**`s_visibleFramePinnedCrops` is a file-static.** The per-frame pin set is a mutable static in `WorkspaceRenderer.cpp`. There is one workspace view today, so it is correct, and `draw()` is not re-entrant. Two views — or a dialog hosting a canvas — would clobber each other's pin sets. It should become renderer instance state.
