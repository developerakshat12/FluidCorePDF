# FluidCore v1.1.7 — Stylus Tool Honesty & Interaction State Recovery

FluidCore **v1.1.7** is a stylus-input correctness release. It fixes a digitizer rewriting the user's selected tool, recovers gestures that were being stranded mid-flight, and repairs the pen barrel button's width bookkeeping. It also removes one feature that was only reachable through the behaviour being fixed.

This release **reverses a documented v1.1.6 behaviour** (see [Breaking Change](#breaking-change-stylus-no-longer-rewrites-the-selected-tool) below). Upgrading users who annotate with a pen will notice the difference immediately.

1. **Stylus Selects Instead of Inking**: A pen now honours the selected tool exactly as a mouse does. Select and Crop work with a digitizer instead of laying down ink.
2. **Stuck Interaction States**: Crop, text-selection, and inking gestures are torn down correctly when a release event is lost, instead of silently swallowing all later input.
3. **Barrel Button Restores Pen Width**: Holding a pen's barrel button no longer pins the ink width at the eraser radius for the rest of the session.
4. **Removed**: Shift-for-highlighter on stylus input.

---

## Breaking Change: Stylus No Longer Rewrites the Selected Tool

### The reported bug

With the **Crop** tool selected, dragging in the document pane intermittently drew a pen stroke instead of selecting a region to crop. The same gesture sometimes worked perfectly. The Select tool was worse: it consistently drew ink whenever a pen was used.

### Root cause

`StylusButtonRouter::resolveInkTool()` existed for exactly one purpose — to rewrite the selected tool to `"pen"` whenever a stylus was the input device. v1.1.6 shipped it covering `select`, `text`, `crop`, and `rect_select`, on the reasoning that a pen press in a selection mode starts a selection and consumes the press, so annotating would otherwise require first switching tools with a mouse.

Two independent mechanisms then combined badly:

- **The crop gate also excluded styluses.** `InkOverlay::onButtonPress` tested `if (!isStylus && (effectiveTool == "crop" ...))`. Even with the tool rewrite removed, a pen could not enter the crop branch at all; it fell through to the inking path and committed a real `AddStrokeCommand` with `stroke.tool == "pen"` to the undo stack and the annotation store.
- **The outcome depended on driver-level device reporting.** The rewrite was keyed on `gdk_device_get_source()`. A Wintab digitizer that GDK reported as `GDK_SOURCE_PEN` inked; the same tablet reported as `GDK_SOURCE_MOUSE` (mouse mode, hover-state transitions, driver quirks) cropped. Identical gesture, two behaviours — which is exactly the intermittency that was reported.

### The fix

The tool-rewriting layer is **deleted** rather than extended with another exclusion. The rule is now stated once, in the `StylusButtonRouter` class comment:

> A stylus is a pointing device, not an inking device: it selects text, drags crop rectangles, and moves cards, exactly as a mouse does.

- `StylusButtonRouter::resolveInkTool()` and its `isMouseOnlyTool()` helper are **removed**. The class now owns only barrel buttons, device classification, and stylus attributes.
- `InkOverlay::resolveToolForDevice()` and the write-only `m_stylusHighlighter` latch are removed. `onButtonPress` reads `m_currentTool` directly.
- The `!isStylus` guard is removed from both the crop and text-selection branches in `onButtonPress`, so a pen genuinely enters those paths.
- `WorkspaceView`'s per-stroke stylus tool override and its `ToolOverrideGuard` unwind are removed. Leaving them would have kept the pen inking over card selection on the infinite canvas, contradicting the rule on the other pane.

### The accepted trade-off

**To ink with a pen you must now select the Pen, Highlighter, or Eraser tool first.** v1.1.6 deliberately avoided that by making the pen always ink.

This is a considered reversal, not an oversight. A digitizer is the most precise pointing device available, which makes it the *best* tool for selecting a region and the *worst* tool for a gesture that a mouse already does adequately. The v1.1.6 trade-off optimised for "never reach for the mouse" and paid for it by making two tools unusable with the hardware they suit most.

### Removed: Shift-for-highlighter

v1.1.6 supported holding **Shift** with a stylus press to get the highlighter nib while a selection tool was active. That path existed only inside `resolveInkTool` and had nowhere to live once the rewrite was deleted. **There is no replacement behaviour** — select the Highlighter tool instead.

`StylusButtonRouterTest`'s `testPenOverridesMouseOrientedTools` was **deleted rather than rewritten**. It tested a function that no longer exists, and the protection is now structural: there is no code left that can rewrite a tool. A vacuous replacement asserting the new behaviour would have been worse than no test.

---

## Stuck Interaction States

`InkOverlay` tracks four independent interaction flags — `m_isDrawing`, `m_isSelectingCrop`, `m_isSelectingText`, and `m_isPotentialExcerptDrag` — and both the motion and release handlers test them in a fixed priority order (crop → text → drawing). Nothing guaranteed they were mutually exclusive, and several teardown paths were missing.

### How a gesture got stranded

`GDK_LEAVE_NOTIFY_MASK` was requested in the constructor but **no `leave-notify-event` handler was ever connected**. When a release was not delivered to the overlay, `onButtonRelease` never ran and the flag stayed `true` for the rest of the session:

- A stranded `m_isSelectingCrop` swallowed **every subsequent release** at the crop branch, which returns early and never reaches the line clearing `m_isDrawing`. The half-finished pen stroke stayed live, kept rendering, and was committed against a later release.
- `InkOverlay::setTool()` cancelled nothing, so switching tools mid-gesture preserved the stale flag: crop → pen mid-drag meant the pen received zero samples; pen → crop mid-stroke meant the pen kept drawing *while the Crop tool was highlighted*, and the release committed a stroke the user believed was a crop.
- `Esc` cleared the crop *selection* but not the crop *drag* flag.
- `onButtonPress` had no "interaction already in flight" guard, so a second device or a duplicate press could set one flag while another was still set.

### The fix

`InkOverlay::cancelCurrentInteraction()` is a single teardown routine, mirroring the existing `WorkspaceView::cancelCurrentInteraction`. It clears all four flags, discards any in-progress stroke, and unwinds a barrel borrow. It is now called from:

- `setTool()` — so switching tools always ends the previous gesture
- `onButtonPress()` — a press implies the previous gesture is over
- `leave-notify-event` — a genuine pointer exit
- `focus-out-event` (new) — how a broken pointer grab usually surfaces
- `proximity-out-event` — the pen leaving digitizer range, which sends no release and was the commoner stranding case
- palm-rejection cancellation — `cancelActiveTouches()` now delegates instead of duplicating the teardown, which had already drifted and missed the barrel borrow

**Grab-mode leaves are deliberately skipped.** GDK holds an implicit grab for the duration of a button press, so a release outside the widget still reaches `onButtonRelease`; cancelling on a `GDK_CROSSING_GRAB` crossing would discard a live stroke every time the pointer merely drifted over the scrollbar. That assumption is now documented at the call site.

### Related: crop motion could fall through into inking

`onMotionNotify`'s crop branch tested `m_dragStartPageIndex < pages.size()` and returned `TRUE` only inside that guard. A relayout between press and motion — zoom, reload, or any of the ~20 `updateLayoutDimensions()` call sites — invalidated the index and let the event fall through into the text and then the ink branch, feeding a crop drag into a pen stroke. The branch now returns unconditionally.

---

## Barrel Button Restores Pen Width

A barrel press wrote `m_currentTool = "eraser"` and `m_currentWidth = kEraserWidth` directly, bypassing `setTool()` and therefore never setting `m_widthForcedByTool`. The release path restored only the tool. **After any barrel press/release the pen stayed pinned at 20.0 pt for the rest of the session.**

Two teardown paths also disagreed: the new `cancelCurrentInteraction()` restored tool *and* width, while the ordinary release restored only the tool — so behaviour depended on which path happened to run.

### The fix

- `InkOverlay::restoreBorrowedBarrelTool()` is one implementation shared by the release path, the tool-change teardown, and the pointer-leave teardown.
- The barrel press now sets `m_widthForcedByTool = true`, so it owns the width it forces.
- The width restore is guarded by `m_widthForcedByTool`, preserving the existing invariant that a width the *user* chose is never clobbered. (An earlier draft of this fix reset the width unconditionally and would have silently discarded a slider-chosen width on any barrel press.)

### Related: a latched eraser blocked all later borrows

`StylusButtonRouter::m_barrelLatched` was **never cleared anywhere in the app**. One long barrel press made `endBarrel()` return `false` permanently, so the borrow could never unwind again. This became load-bearing with the shared restore above, which would have been a permanent no-op. `clearBarrelLatch()` is now called when the user explicitly selects a non-eraser tool — an explicit tool choice is the user saying the eraser is no longer wanted. Covered by `testClearBarrelLatchAllowsAFurtherBorrow`.

---

## Files Changed

| Area | Change |
| :--- | :--- |
| `src/app/services/StylusButtonRouter.{h,cpp}` | Removed `resolveInkTool()` and `isMouseOnlyTool()`. Added `clearBarrelLatch()`. Class comment now records the stylus-is-a-pointing-device rule and the reversal. |
| `src/app/document/InkOverlay.{h,cpp}` | Added `cancelCurrentInteraction()`, `restoreBorrowedBarrelTool()`, `leave-notify-event` and `focus-out-event` handlers. Removed `resolveToolForDevice()` and `m_stylusHighlighter`. Crop and text branches no longer exclude styluses. |
| `src/app/document/DocumentPane.{h,cpp}` | Added `cancelCurrentInteraction()` passthrough. |
| `src/app/workspace/WorkspaceView.{h,cpp}` | Removed the per-stroke stylus tool override, `ToolOverrideGuard`, and its three now-dead members. |
| `src/app/main.cpp` | `Esc` now cancels in-flight interactions before clearing selections. |
| `src/app/tests/StylusButtonRouterTest.cpp` | Deleted `testPenOverridesMouseOrientedTools`; added `testClearBarrelLatchAllowsAFurtherBorrow`; extended the crop/rect_select assertions. |

Additionally, `specs/file-function-map.md` was corrected to stop asserting the reversed behaviour in its `InkOverlay` and `StylusButtonRouter` entries. That file is **not part of the repository** — `/specs/` is listed in `.gitignore` alongside `CLAUDE.md`, `/planning/`, `/references/`, `/skills/`, and `/tasks/` as agent working files — so the correction is local to this working tree and does not appear in the published source.

**Tests: 42/42 CTest suites pass.** Formatting is clean against the CI `clang-format` rules.

---

## Known Gap

**`InkOverlay::onButtonPress` routing has no automated coverage.** No test constructs an `InkOverlay` or drives a press/motion/release sequence, so the tool-routing logic that contained both original bugs is unprotected: re-adding a stylus exclusion there would pass all 42 suites green. The protection that exists today is structural — no code remains that can rewrite a tool — but that does not cover the branch routing itself.

Closing this needs a test that builds a real `GdkEventButton` with a chosen `GdkDevice` source and asserts which branch is taken. Tracked as follow-up work.
