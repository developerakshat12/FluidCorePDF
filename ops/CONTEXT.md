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
4. **Windows Release Packaging & CI Checklist**:
   - Both `.github/workflows/ci.yml` and `.github/workflows/release.yml` MUST install:
     `mingw-w64-ucrt-x86_64-librsvg`, `mingw-w64-ucrt-x86_64-adwaita-icon-theme`, `mingw-w64-ucrt-x86_64-hicolor-icon-theme`, and `mingw-w64-ucrt-x86_64-gtk-update-icon-cache`.
   - **Cairo Version Pinning (Regression Protocol)**: Both workflows MUST pin Cairo to stable `1.18.4-4` via `pacman -U https://repo.msys2.org/mingw/ucrt64/mingw-w64-ucrt-x86_64-cairo-1.18.4-4-any.pkg.tar.zst`.
     - *Symptom:* Application launches, opens a blank window or splash, and crashes within 1–2 seconds with Windows exit code `0xC0000409` (`STATUS_STACK_BUFFER_OVERRUN`) in `ucrtbase.dll!abort()`. `CropDragCrashTest` fails during font rasterization.
     - *Root Cause:* Upstream Cairo 1.18.6+ introduced a fatal assertion abort in `cairo-colr-glyph-render.c:1168: assert (!"reached");` when the Windows DirectWrite backend queries system fonts containing COLRv1 color glyph tables (e.g. `Segoe UI Emoji`).
     - *Diagnostic Check:* Run `Select-String "cairo-1.18.6" libcairo-2.dll` or run under GDB: `b abort` -> backtrace will show `libcairo-2.dll` in the call stack above `pango_cairo_show_layout`.
     - *Remediation Command:*
       ```bash
       curl -sSL --retry 3 "https://repo.msys2.org/mingw/ucrt64/mingw-w64-ucrt-x86_64-cairo-1.18.4-4-any.pkg.tar.zst" -o /tmp/cairo.pkg.tar.zst
       pacman -U --noconfirm /tmp/cairo.pkg.tar.zst
       ```
   - `CropDragCrashTest` actively executes `WorkspaceRenderer::draw` against an image surface to smoke-test Pango/Cairo font rasterization and detect upstream font table regressions before releases.
   - `package-windows.ps1` must execute `gdk-pixbuf-query-loaders.exe` with relative paths so `loaders.cache` correctly maps `pixbufloader_svg.dll`.
   - `package-windows.ps1` contains an automated binary string scan ensuring `cairo-1.18.6` is never bundled into distributions.
   - `src/app/main.cpp` must resolve the application directory via `GetModuleFileNameW(NULL, ...)` to ensure `GSETTINGS_SCHEMA_DIR`, `FONTCONFIG_PATH`, and `GDK_PIXBUF_MODULE_FILE` point to bundled resources.

## Avoid
- Merging a regression past budget without an ADR documenting the trade-off
- Changing `package-windows.ps1` dependencies without updating `.github/workflows/release.yml` and `.github/workflows/ci.yml` simultaneously.
