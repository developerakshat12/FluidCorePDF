# Ops Workspace

Last updated: 2026-09-05

## What this is for
Build, CI, benchmarks, packaging, release. Everything that keeps the project shippable and its perf budgets measurable.

## Current layout
```
ops/
├── CONTEXT.md            # this file
├── benchmarks/           # bench-scalability.md (50-PDF 5000-page cold start & memory budget)
├── flatpak/              # org.fluidcore.platform.yml (Flathub-compliant sandboxed manifest)
├── installer/            # fluidcore.iss (Inno Setup 64-bit native Windows installer script)
├── patches/              # 0001-msys2-glib-mkenums-python-fix.patch, poppler_win32_critical_section.patch, upstream/
└── scripts/              # build-linux.sh, build-win.ps1, build_patched_poppler.ps1, monitor.ps1, run-probe.ps1, run-scenario-repeated-find.ps1, package-windows.ps1, etc.
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
