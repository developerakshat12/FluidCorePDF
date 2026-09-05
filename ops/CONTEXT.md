# Ops Workspace

Last updated: 2026-09-05

## What this is for
Build, CI, benchmarks, packaging, release. Everything that keeps the project shippable and its perf budgets measurable.

## Current layout
```
ops/
├── CONTEXT.md            # this file
├── benchmarks/           # bench-scalability.md (50-PDF 5000-page cold start & memory budget)
├── installer/            # fluidcore.iss (Inno Setup 64-bit native installer script)
└── scripts/              # build-win.ps1, package-windows.ps1, check_budgets.py, check_invariants.py
```

## Standing gates (from ROADMAP §5 — these are release blockers)
| Metric | Budget |
|--------|--------|
| Inking latency | ≤ 20 ms |
| Squeeze FPS | ≥ 30 sustained @1080p |
| Spatial query p99 | ≤ 1 ms @ 10⁵ items |
| Cold load, 50-PDF project | ≤ 8 s |
| RAM working set | ≤ 1.2 GB |

## Process
1. Perf-gated PRs attach `benchmarks/bench-<area>.md` with machine specs + numbers vs. budget
2. Release checklist per milestone exit lives here once M0 lands
3. Offline guarantee: CI syscall audit must show zero runtime network access

## Avoid
- Merging a regression past budget without an ADR documenting the trade-off
