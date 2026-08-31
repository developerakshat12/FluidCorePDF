# FluidCore Platform

An **open-source, offline-first alternative to LiquidText** for active reading: a fluid, malleable PDF reader fused with an infinite 2D synthesis workspace. Built as a decoupled C++20 engine (`libfluidcore`) with a GTK 3 / Cairo / Poppler desktop frontend.

## Why
Standard PDF viewers lock content into rigid pages. This project lets you:
- **Squeeze** non-adjacent pages together (two-finger pinch or `Ctrl+Shift+Scroll`) and read them side-by-side
- **Drag excerpts** — text, figures, equations — onto an infinite canvas while keeping a live bi-directional link to the source
- **Connect and stack** excerpts with ink links and topic stacks
- Do all of it **100% offline**, stored in a portable `.ltproj` (SQLite WAL) bundle

## Repository Structure (ICM 3-layer architecture)
Start at **[CLAUDE.md](CLAUDE.md)** — it routes every task to the right workspace.

| Workspace | Purpose | Entry point |
|-----------|---------|-------------|
| `planning/` | PRD, TRD, ROADMAP, MVP scope, ADRs | [planning/CONTEXT.md](planning/CONTEXT.md) |
| `specs/` | Features, architecture, flows, file map | [specs/CONTEXT.md](specs/CONTEXT.md) |
| `src/` | Code: `libfluidcore/` engine + GTK app | [src/CONTEXT.md](src/CONTEXT.md) |
| `ops/` | CI, benchmarks, packaging | [ops/CONTEXT.md](ops/CONTEXT.md) |
| `references/` | ICM method, upstream notes, doc index | [references/REFERENCES.md](references/REFERENCES.md) |
| `skills/` | Layer-3 skills wired via routing table | [skills/README.md](skills/README.md) |

Machine-readable state: [project.yaml](project.yaml) · [planning/roadmap.yaml](planning/roadmap.yaml) · [planning/backlog.yaml](planning/backlog.yaml)

## Status
Pre-alpha — Milestones M0–M4 complete (Reader core, Squeeze engine, Infinite workspace & excerpts, Bi-directional anchors, Links, Stacks, Search & Export); M5 (Hardening) in progress. See [planning/ROADMAP.md](planning/ROADMAP.md).

## License
GPL-2.0-or-later (inherited from Xournal++); `libfluidcore` relicensing tracked in [GOVERNANCE.md §3](GOVERNANCE.md). Contributions: see [CONTRIBUTING.md](CONTRIBUTING.md).
