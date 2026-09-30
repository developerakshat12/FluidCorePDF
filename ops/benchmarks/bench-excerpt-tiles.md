# Excerpt Tile Cache — Sizing & Dispatch Contract

**Purpose.** Record the tile-sizing contract introduced in v1.1.8 and the dispatch-cancellation behaviour, with every number labelled by how it was obtained. Per `ops/CONTEXT.md`, a render-path change must ship a benchmark artifact; where a figure is derived rather than measured, it says so.

**Machine.** 12th Gen Intel Core i5-12500H, 12 cores / 16 threads, 15.6 GB RAM, Windows 11 Home Single Language build 26200.
**Toolchain.** MSYS2 UCRT64, GCC 16.2.0, Cairo **1.18.4-4** (pin enforced by `ops/CONTEXT.md`; verified installed via `pacman -Qi`), Poppler GLib 26.08.0-1.

---

## 1. How to read the numbers

Three different kinds of claim appear below, and they are not interchangeable:

- **Derived** — computed from `TileSizingPolicy::computeAspectPreservingDimensions` for a stated input. Reproducible; no execution required.
- **Asserted** — enforced by a named test in `src/app/tests/ExcerptTileCacheTest.cpp`. A failure fails CI.
- **Not measured** — believed true, but not currently verified by anything. Stated as a gap rather than as a result.

There are no FPS or frame-time figures in this document. This change runs entirely on the background worker path and off the UI critical path, so FPS would be the wrong instrument; the meaningful quantities are bytes per tile and wasted rasterizations.

---

## 2. Tile sizing across all five LOD tiers — derived

Input is the reported session: a 4:3 crop of `648 × 486` PDF points on a card body of `450 × 337.5` points. True crop aspect is `1.333333`. Box is the card body multiplied by the tier's LOD scale; tile is what `TileSizingPolicy` returns.

| Tier | LOD scale | Requested box | Resulting tile | Tile bytes | Aspect deviation |
| :--- | ---: | :--- | :--- | ---: | ---: |
| `Overview` | 0.5× | 225 × 169 | 225 × 169 | 152,100 | 0.1479% |
| `Standard` | 1.0× | 450 × 338 | 450 × 338 | 608,400 | 0.1479% |
| `HiDpi` | 2.0× | 900 × 675 | 900 × 675 | 2,430,000 | 0.0000% |
| `Retina` | 4.0× | 1800 × 1350 | 1182 × 887 | 4,193,376 | 0.0564% |
| `Ultra` | 8.0× | 3600 × 2700 | 1182 × 887 | 4,193,376 | 0.0564% |

Two things worth recording honestly:

**The maximum deviation is 0.1479%, not 0.06%.** It occurs at `Overview` and `Standard`, where the tile is small enough that ±0.5 px of integer rounding on the short axis is a visible fraction of a percent. 0.06% is the *`Retina`* figure and is the best case, not the worst. An earlier revision of this document reported 0.06% as the maximum, which was wrong in the flattering direction.

**The byte ceiling binds before the dimension ceiling.** `Retina` asks for 1800 × 1350; the 1536 px cap reduces the width to 1536 first (giving 1536 × 1152 = 1,769,472 px), and the 4 MiB ceiling then reduces that to 1182 × 887 = 4,193,376 B = 3.9985 MiB. The post-round guard is load-bearing: rounding can push the product back over the limit, and a 1449 × 724 tile would be 4,196,304 B against a 4,194,304 B ceiling.

For comparison, the same card under the pre-v1.1.8 pipeline produced a `Retina` tile of 1536 × 1390 = 8,540,160 B (8.14 MiB) at −17.12% aspect error. The v1.1.8 tile is 2.04× smaller and 16.6× closer to the correct aspect.

`Ultra` is unreachable in the current build: `kMaxZoom = 2.0` (`src/app/workspace/WorkspaceView.cpp:29`) makes `Retina` the top reachable tier. It is retained here because the ladder still defines it and a future zoom-ceiling change would activate it.

## 3. Sizing contract — asserted

| Contract | Test |
| :--- | :--- |
| Aspect preserved across all five tiers for 4:3, 16:9, 1:1 and 3:4 crops; relative deviation < 0.02 | `testTileAspectPreservedAtEveryTier` |
| `Retina` request respects both the 1536 px dimension cap and the 4 MiB byte cap simultaneously | `testRetinaTierRespectsDimensionAndByteCeiling` |
| The **asynchronous** path honours the 4 MiB ceiling (the pre-v1.1.8 gap) | `testAsyncPathEnforcesPerTileByteCeiling` |
| A 5 × 700 pt sliver crop keeps its true 1:140 ratio, proving the 1 px floor and not a 16 px per-axis floor | `testTileAspectPreservedAtEveryTier` |
| Visible crops are never evicted | `testVisibleCropPinnedAgainstEviction` |
| Pinned crops survive the idle trim to the 6 MiB slice | `testIdleBudgetDoesNotEvictPinnedCrops` |
| Card body geometry agrees between layout, render rect and tile request | `testCanonicalGeometryConsistency` |

The suite's aspect tolerance is 0.02 relative, not tighter, because pixel rounding on the small tiers produces ~0.0015 and any threshold below that would be asserting floating-point luck rather than the contract. The 0.1479% worst case above is the real headroom being used.

## 4. Stale-tier dispatch cancellation — asserted

Zooming across a LOD boundary queues a render per visible card at the tier being left. Those tasks are now cancelled at dispatch, before any rasterization.

| Contract | Test |
| :--- | :--- |
| Dispatching tier *N* cancels a queued tier *M* for the same crop | `testStaleTierCancellationAtDispatch` |
| The worker bails **before** Poppler: `skippedRasters ≥ 1` | `testStaleTierCancellationAtDispatch` |
| No raster completes then gets discarded: `droppedRasters == 0` | `testStaleTierCancellationAtDispatch` |
| No leaked request bookkeeping: `activeRequests == 0` | `testStaleTierCancellationAtDispatch` |
| Resize purges the resized tier so a re-render dispatches (no size lock-in) | `testResizeCommitRetainsFallbackSurface` |
| The retained fallback keeps its authentic tier (no re-keying) | `testResizeCommitRetainsFallbackSurface` |

`testStaleTierCancellationAtDispatch` is deterministic by construction, not by timing: it holds `globalPopplerMutex()` and occupies both `GThreadPool` workers with dummy tasks before dispatching, so the superseded task provably cannot reach Poppler. Without that, the assertion would pass or fail on scheduling luck.

The two counters mean opposite things and are kept separate deliberately. `skippedRasters` counts rasters **avoided**; `droppedRasters` counts rasters **completed and then discarded** by a cancellation that arrived after the worker passed its check. A single combined counter cannot distinguish a working optimization from a failing one.

### Telemetry

`dumpStats` now emits both counters, reachable under `FLUIDCORE_LOG_TELEMETRY=1`. The output format is:

```
[ExcerptTileCache] === <tag> === Entries: <n> | Bytes: <used>/<max> | Active Req: <n> | Skipped Rasters: <n> | Dropped Rasters: <n> | Tiers: [<tier breakdown>]
```

**Not measured:** no captured run of the application under `FLUIDCORE_LOG_TELEMETRY=1` is recorded in this document. The counters are verified by the assertions in §4, not by an observed session trace. An earlier revision of this file presented a sample log line here as "Real Telemetry Log Output"; it was hand-written to illustrate the format and was removed rather than left in place, because a plausible-looking log line is indistinguishable from a captured one to a later reader.

## 5. Known gaps

**End-to-end aspect verification does not run on Windows.** `testRealPdfCropRendering` resolves its fixture from a POSIX path (`/mnt/d/study material/…`) and silently skips on this platform, so the only assertion exercising `renderBackgroundCrop` against a real PDF never runs in Windows CI. The policy itself is covered directly through `TileSizingPolicy` and through `testAsyncPathEnforcesPerTileByteCeiling` against a synthetic PDF, so the contract in §2 is enforced — but a regression in the Poppler-facing path specifically would not be caught on the primary platform. Closing this means generating the fixture in-test, as `CropDragCrashTest` and `PdfExportServiceTest` already do.

**No end-to-end frame-timing or interaction-latency measurement was taken.** The change touches background rasterization only, so this is deliberate, but it does mean the user-visible smoothness claim in the release notes rests on the elimination of the placeholder rather than on a measured improvement.

**Cards saved before v1.1.8 pick up ~3 pt of letterboxing** until moved or resized, because `computeExcerptCardDimensions` changed from `imgW + 28` / `imgH + 46` to `+32` / `+40` and persisted card bounds were computed with the old constants. There is no distortion — body rect and tile request share one helper — and no schema migration was added. Documented in `ops/RELEASE_NOTES_v1.1.8.md`.

**`bench-scalability.md` is unchanged by this release.** Its 50-document working-set figures exercise `renderCropSync`, whose tiles are now smaller (fit-inside rather than the requested box), but the page-tile cache dominates that measurement, so the excerpt change is below the noise floor of the benchmark. No delta is claimed.
