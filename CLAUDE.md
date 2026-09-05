# FluidCore Platform

Open-source, offline-first alternative to LiquidText: fluid PDF reader + infinite synthesis workspace. Decoupled C++20 engine (`libfluidcore`) + GTK 3 / Cairo / Poppler desktop frontend. Pre-release (M0–M4 and M5.5 Windows port complete; M5 hardening in progress; Windows native installer and portable release live).

## Folder Structure
- `/planning` — PRD, TRD, ROADMAP, MVP scope, ADRs (why & what)
- `/specs` — feature spec, architecture, flows, file map (how it works)
- `/src` — actual code: `libfluidcore/` (engine, no GTK) and `app/` (GTK frontend)
- `/ops` — CI, packaging, benchmarks, release scripts
- `/references` — ICM method notes, upstream Xournal++ pointers, research
- `/skills` — Layer 3 skill registry (load only when routed to)

## Routing
| Task | Go to | Read | Skills |
|------|-------|------|--------|
| Product/scope/roadmap questions | /planning | CONTEXT.md | — |
| Understand a feature or architecture | /specs | CONTEXT.md | — |
| Write/change engine code | /src/libfluidcore | src/CONTEXT.md | cpp-core-dev |
| Write/change GUI code | /src/app | src/CONTEXT.md | gtk-frontend |
| Verify perf budgets | /ops | ops/CONTEXT.md | perf-gate |
| Implement a spec'd milestone item | /planning → /src | both CONTEXT.md | spec-to-code |
| Release/packaging/CI | /ops | CONTEXT.md | — |
| Background/upstream questions | /references | REFERENCES.md | — |

## Naming Conventions
- Specs: `<feature-name>_spec.md`; decisions: `YYYY-MM-DD-decision-title.md` in `planning/decisions/`
- Code: C++20, files PascalCase matching class (`SqueezeEngine.cpp/h`)
- Tests: `<Class>Test.cpp` mirroring source path
- Benchmarks: `bench-<area>.md` in `/ops/benchmarks`

## Rules
- Read this file first on every new task; follow the routing table — do NOT read every doc
- When the user says "commit", that means commit AND push (both, every time)
- `libfluidcore/` must never include GTK headers (ADR-0001)
- Perf budgets in planning/ROADMAP.md §5 are merge gates
- Never introduce network calls at runtime (offline-first guarantee)
- Ask before creating files outside the workspace you were routed to
- Do not change gitignore you can only suggest the changes to made

## Common Gotchas & Troubleshooting
- **GTK Container Lifecycle**: Root widgets returned by `widget()` (such as `m_viewOverlay` in `DocumentPane`) MUST be instantiated at the top of the constructor before any early returns (e.g. on invalid PDF paths). Returning `nullptr` breaks GTK window hierarchy mapping and prevents windows from appearing.
- **Native Windows Build & Run (MSYS2 UCRT64)**:
  FluidCore runs natively on Windows 11. Use `ops/scripts/build-win.ps1` or `ops/scripts/package-windows.ps1`:
  ```powershell
  # Build and run all 36 CTest suites
  powershell -ExecutionPolicy Bypass -File ops/scripts/build-win.ps1 -Test
  # Launch app natively with document
  powershell -ExecutionPolicy Bypass -File ops/scripts/build-win.ps1 -Run -Document "D:\path\to\doc.pdf"
  # Package standalone zip distribution and Inno Setup installer
  powershell -ExecutionPolicy Bypass -File ops/scripts/package-windows.ps1 -BuildInstaller
  ```
- **Running via WSL from PowerShell**: Avoid wrapping commands with nested quotes in `wsl bash -c "..."` because PowerShell strips inner quotes around paths with spaces (e.g., `study material`). Instead, invoke directly:
  ```powershell
  wsl -d Ubuntu --cd "<workspace>/build-wsl" ./src/app/fluidcore_app "<path-to-pdf>"
  ```
  FluidCore's `normalizePath` automatically maps Windows drive paths (`D:\...`) into WSL mount paths (`/mnt/d/...`).

## Full Doc Map
See `references/REFERENCES.md` for one-line summaries of every document.
