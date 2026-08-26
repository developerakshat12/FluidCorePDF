# Skill: GTK Frontend Development

## Purpose
Build GUI correctly on top of libfluidcore without violating layer boundaries.

## Inputs
- `specs/system-architecture.md` (view/controller layers), `specs/appflow.md` (state machines)

## Procedure
1. Locate the right file via `specs/file-function-map.md` (XournalView, WorkspaceView, PageView, ReturnAnchorPill, handlers)
2. Business logic goes in controllers/engine calls — NEVER in event callbacks
3. Every touch gesture must have mouse/keyboard parity (MVP D2): pinch → Ctrl+Shift+Scroll + margin pins
4. Rendering must use dirty-rect invalidation; full redraws only on structural change
5. Verify against the appflow state machine: every transition reachable and reversible

## Done criteria
- Manual test on reference hardware at 1080p
- Keyboard-only path demonstrated for any new interaction
- No new direct Poppler/Cairo calls outside established view classes without ADR
