# Ops Workspace

Last updated: 2026-08-26

## What this is for
Build, CI, benchmarks, packaging, release. Everything that keeps the project shippable and its perf budgets measurable.

## Planned layout (fills in during M0)
```
ops/
├── CONTEXT.md            # this file
├── ci-pipeline.md        # CI design: build matrix, sanitizers, format check, syscall audit
├── benchmarks/           # bench-<area>.md artifacts attached to perf-gated PRs
├── packaging/            # Flatpak / AppImage / .deb manifests; Windows MSYS2 notes
└── scripts/              # local dev helpers
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
