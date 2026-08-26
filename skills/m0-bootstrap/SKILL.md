# Skill: Agent Bootstrap (m0-bootstrap)

## Purpose
Mandatory first read for any autonomous agent picking up a `tasks/TASK-*.yaml` card.
Gets you productive inside this repo's conventions without re-deriving them.

## Inputs
- Your assigned task card: `tasks/TASK-<id>.yaml`
- This skill, plus the files it points to

## Procedure
1. Read `/CLAUDE.md` (routing table + rules) and `src/CONTEXT.md` (code conventions)
2. Read your task card end-to-end; note `acceptance_checks` — these define done
3. Read the specs your card's `specs` field lists — cite section numbers in commits
4. Work in a vertical slice: code + tests + spec touch-up in one change
5. Verify locally before pushing:
   - Engine work: `cmake --build build && ctest --test-dir build --output-on-failure`
   - Format: `clang-format --dry-run --Werror` on every file you touched (CI pins 18.1.8)
   - Invariants: `python ops/scripts/check_invariants.py`
   - App/GTK work: cannot compile on the Windows host (no GTK); CI's `app-shell`
     job is the compiler — push, then `gh run watch` and fix until green
6. Commit with DCO sign-off (`git commit -s`) and a message citing the task id
7. "Commit" means commit AND push (repo rule, CLAUDE.md)

## Hard rules (violation = rejected PR)
- `libfluidcore/` never includes GTK/GDK/GLib/Cairo/Poppler headers (ADR-0001)
- Engine changes REQUIRE headless unit tests mirroring the source path
- No network calls in runtime code (GOVERNANCE §5)
- No business logic in GTK callbacks
- Update `specs/file-function-map.md` when adding modules/files
- Never commit without `-s` (DCO trailer matching author email)

## Done criteria
- Every `acceptance_checks` item in your task card verifiably passes
- CI fully green on the pushed commit (`gh run list` shows success)
- Report: what changed, what was verified, any deferred decisions (with reasons)
