<!--
Keep in sync with CONTRIBUTING.md. If the two disagree, CONTRIBUTING.md wins
and this template is the bug.
-->

## What this changes

<!-- One sentence. If you cannot write this, the PR may be too broad. -->

Fixes #

## Type

`feat` | `fix` | `perf` | `refactor` | `test` | `docs` | `chore`

## Checklist

- [ ] I opened or claimed an issue and it maps to a persona → PRD requirement → milestone.
- [ ] `libfluidcore/` still contains **no GTK headers** (ADR-0001). This is checked in CI and is a hard invariant.
- [ ] Engine changes have headless unit tests. Coverage may not decrease.
- [ ] Anything touching squeeze, spatial-index, or render paths has a benchmark artifact attached. These are perf-gated and need **two** approvals ([GOVERNANCE.md §2](https://github.com/developerakshat12/FluidCorePDF/blob/main/GOVERNANCE.md)).
- [ ] I ran `clang-format` and the working tree is clean.
- [ ] Performance budgets in ROADMAP §5 still pass. If a budget regressed, the PR includes a trade-off ADR.
- [ ] User-visible behavior changes are reflected in docs.
- [ ] Architectural, licensing, or `.ltproj`-schema changes have an ADR.

## Testing

<!--
How did you verify this? For input-handling changes, state the device you
tested on and whether you confirmed GDK's view of it — device source
classification (pen vs mouse) is a recurring source of bugs that pass CI.
-->

- [ ] I ran the relevant CTest suites locally and they pass.
- [ ] For GUI/input changes, I tested the real app, not just tests.

Manual verification performed:

## Compatibility notes

- [ ] **No** upgrade or migration concerns.
- [ ] Yes — describe below (e.g. `.ltproj` schema change, changed file format, changed default).

## Screenshots / recordings

<!-- Required for user-visible UI changes. -->
