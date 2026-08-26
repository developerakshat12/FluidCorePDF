# Contributing to the Fluid Document Synthesis Platform

Thank you for helping build an open-source alternative to LiquidText. This document covers setup, workflow, and standards.

## 1. Code of Conduct
By participating you agree to abide by the [Contributor Covenant v2.1](https://www.contributor-covenant.org/version/2/1/code_of_conduct/). Reports go to the maintainers listed in GOVERNANCE.md.

## 2. Development Setup (Linux)

### Prerequisites
- GCC ≥ 12 or Clang ≥ 16
- CMake ≥ 3.20, Ninja
- GTK 3 dev headers, Cairo, Poppler-GLib ≥ 22.x, SQLite 3 (FTS5 enabled)
- `clang-format`, `clang-tidy`

### Build
```bash
git clone --recurse-submodules https://github.com/<org>/fluidsynth-docs.git
cd fluidsynth-docs
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
cmake --build build
ctest --test-dir build          # unit tests incl. libfluidcore headless tests
./build/xournalpp               # run app
```
First successful build should take ≤ 15 minutes (see ROADMAP M0 exit criteria).

## 3. Repository Layout
```
libfluidcore/        # standalone C++20 core engine (no GUI deps)
  workspace/         # scene graph + R*-tree spatial index
  squeeze/           # piecewise coordinate mapper
  graph/             # GraphTopology, Bezier router
  storage/           # SQLite WAL ProjectStore (.ltproj)
src/core/            # Xournal++ integration layer (GTK frontend)
```
**Rule**: `libfluidcore` must never include GTK headers. It is tested headlessly.

## 4. Workflow
1. Open or claim a GitHub issue; comment `taking` to self-assign.
2. Branch from `main`: `<type>/<issue#>-short-desc` (e.g., `feat/#142-squeeze-pins`).
3. Types: `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `chore`.
4. Every PR needs:
   - Unit tests for engine changes (`libfluidcore` tests are mandatory; coverage may not decrease)
   - Benchmarks for anything touching squeeze/spatial-index/render paths
   - Updated docs if behavior is user-visible
5. CI must pass: build matrix, sanitizers, clang-format check.
6. One maintainer review required; perf-gated areas need two (see CODEOWNERS).

## 5. Code Standards
- C++20 for `libfluidcore`; follow existing Xournal++ style in `src/core`.
- Formatting enforced via `.clang-format` — run `make format` before committing.
- No comments restating code; explain *why* only where non-obvious.
- Performance budgets in ROADMAP §5 are gates: a PR that regresses a benchmark beyond budget will not merge without a documented trade-off ADR.

## 6. Architecture Decision Records
Substantial design changes require an ADR in `docs/adr/NNNN-title.md` using the standard template (Context / Decision / Consequences). Existing ADRs are binding until superseded.

## 7. Issue & Bug Reports
Use the templates:
- **Bug**: OS/GTK version, hardware (digitizer model if input-related), repro steps, `.ltproj` sample if safe.
- **Feature**: reference the relevant section of PRD.md/feature.md, or argue why it should be added there.
- **Perf**: include benchmark numbers and machine specs.

## 8. Licensing
- Overall project inherits GPL-2.0-or-later (Xournal++ lineage).
- Contributions to `libfluidcore` are made under the same license unless the directory is explicitly re-licensed per GOVERNANCE.md; do **not** link GPL-only symbols into it.

## 9. Communication
- GitHub Discussions: design proposals and Q&A
- Matrix room: `#fluid-docs:matrix.org` (bridged to Discussions digest)
- Release cadence: minor every ~10 weeks aligned to milestone exits.
