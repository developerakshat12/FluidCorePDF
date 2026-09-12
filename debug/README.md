# FluidCore Platform: Debug & Diagnostics Workspace

This workspace consolidates all developer diagnostic tooling, live memory telemetry headers, verification probes, post-mortem bug reports, and root-cause investigation archives.

---

## Directory Organization

```
debug/
├── README.md               # Master navigation index (this file)
├── telemetry/              # Live process memory & telemetry tracking headers
│   └── MemoryTelemetry.h   # Windows heap/LFH metrics, mimalloc tracking & telemetry logger
├── scripts/                # Diagnostic PowerShell scripts
│   ├── monitor.ps1         # Live Win32 private bytes, working set & event timeline monitor
│   └── run-probe.ps1       # Runner for search isolation probe
├── probes/                 # Standalone empirical diagnostic binaries
│   └── SearchIsolationProbe.cpp # Out-of-process vs in-process search probe with IAT hooking
├── reports/                # Catalog of 20 verified historical bug reports
│   ├── bug_report_*.md     # Post-mortem analyses indexed across 5 major subsystems
├── investigations/         # Deep-dive root cause analyses & audit chronologies
│   ├── MEMORY_LEAK_INVESTIGATION_CHRONOLOGY_AND_TEST_CASES.md # Complete multi-phase leak chronology
│   └── context.md          # Navigation index for the memory leak chronology
└── logs/                   # Target directory for runtime telemetry output logs (*.log ignored)
    └── .gitkeep
```

---

## 1. Telemetry (`debug/telemetry/`)

- **[`MemoryTelemetry.h`](file:///d:/FluidCorePDF/fluidcore-platform/debug/telemetry/MemoryTelemetry.h)**:
  - Tracks process Working Set (`K32GetProcessMemoryInfo`), Private Bytes, native Windows CRT Heap Commit (`HeapWalk` / `HEAP_ENTRY_BUSY`), and mimalloc internal metrics (`mi_process_info`).
  - Gated by environment flags: `FLUIDCORE_LOG_TELEMETRY`, `FLUIDCORE_HEARTBEAT`, `FLUIDCORE_SEARCH_BENCHMARK`, and `FLUIDCORE_VERBOSE_TELEMETRY`.
  - Automatically writes runtime logs to `debug/logs/fluidcore_telemetry.log` when present.

---

## 2. Diagnostic Scripts (`debug/scripts/`)

- **[`monitor.ps1`](file:///d:/FluidCorePDF/fluidcore-platform/debug/scripts/monitor.ps1)**:
  - Real-time console dashboard sampling working set, private commit, and CPU usage at high frequency.
  - Automatically invoked via `build-win.ps1 -Monitor` or `-Launch -Monitor`.
- **[`run-probe.ps1`](file:///d:/FluidCorePDF/fluidcore-platform/debug/scripts/run-probe.ps1)**:
  - CLI runner for the standalone `search_isolation_probe.exe`.

---

## 3. Diagnostic Probes (`debug/probes/`)

- **[`SearchIsolationProbe.cpp`](file:///d:/FluidCorePDF/fluidcore-platform/debug/probes/SearchIsolationProbe.cpp)**:
  - Standalone executable compiled via CMake target `search_isolation_probe` (part of `poppler_tests`).
  - Features isolated private heaps (`HeapCreate`) and Import Address Table (IAT) hooking across `libpoppler-glib-8.dll`, `libpoppler-163.dll`, and `libwinpthread-1.dll`.

---

## 4. Bug Reports Archive (`debug/reports/`)

Historical bug reports indexed across 5 core subsystems:

### Rendering, Caching & Memory
- [`bug_report_large_pdf_oom_and_unbounded_draw_clipping.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_large_pdf_oom_and_unbounded_draw_clipping.md)
- [`bug_report_save_project_as_crash_and_residual_oom.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_save_project_as_crash_and_residual_oom.md)
- [`bug_report_workspace_renderer_radius_redeclaration.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_workspace_renderer_radius_redeclaration.md)

### Search & Document Lifecycle
- [`bug_report_search_target_classification_precedence.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_search_target_classification_precedence.md)
- [`bug_report_workspace_search_stub_api_compilation.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_workspace_search_stub_api_compilation.md)
- [`bug_report_startup_crash_unregistered_squeeze_document.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_startup_crash_unregistered_squeeze_document.md)

### Inking, Stylus & Tools
- [`bug_report_eraser_bounding_box_false_trigger.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_eraser_bounding_box_false_trigger.md)
- [`bug_report_eraser_drag_cursor_latency_and_redraw_gating.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_eraser_drag_cursor_latency_and_redraw_gating.md)
- [`bug_report_vector_pen_stroke_persistence_and_safe_loader.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_vector_pen_stroke_persistence_and_safe_loader.md)
- [`bug_report_card_movement_and_tool_locking.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_card_movement_and_tool_locking.md)

### UI Chrome, GTK3 & Window Management
- [`bug_report_gtk_critical_queue_draw.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_gtk_critical_queue_draw.md)
- [`bug_report_gtk_critical_top_toolbar_widget.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_gtk_critical_top_toolbar_widget.md)
- [`bug_report_right_click_context_menu_lockout.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_right_click_context_menu_lockout.md)
- [`bug_report_canvas_double_click_viewport_jump.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_canvas_double_click_viewport_jump.md)
- [`bug_report_stack_rename_lag_and_dnd_hierarchy.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_stack_rename_lag_and_dnd_hierarchy.md)
- [`bug_report_cross_pane_undo_routing.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_cross_pane_undo_routing.md)

### OS Integration & Environment
- [`bug_report_assert_ndebug_project_store_test.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_assert_ndebug_project_store_test.md)
- [`bug_report_pdf_export_ui_blocking_and_lifetime.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_pdf_export_ui_blocking_and_lifetime.md)
- [`bug_report_wsl2_virtual_storage_root_in_file_chooser.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_wsl2_virtual_storage_root_in_file_chooser.md)
- [`bug_report_wslg_window_maximize_skew_and_csd_shadows.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/reports/bug_report_wslg_window_maximize_skew_and_csd_shadows.md)

---

## 5. Investigation Archives (`debug/investigations/`)

- **[`MEMORY_LEAK_INVESTIGATION_CHRONOLOGY_AND_TEST_CASES.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/investigations/MEMORY_LEAK_INVESTIGATION_CHRONOLOGY_AND_TEST_CASES.md)**:
  - Complete chronological record of the multi-phase memory leak investigation from initial 600 MB discovery to final native Win32 `CRITICAL_SECTION` resolution.
- **[`context.md`](file:///d:/FluidCorePDF/fluidcore-platform/debug/investigations/context.md)**:
  - Exact line-by-line navigation index for the investigation chronology.

---

## Canonical Upstream Remediation Document

For the authoritative production Poppler remediation summary, upstream patch files, and ABI verification, see:
- [`docs/POPPLER_MUTEX_LEAK_REMEDIATION_AND_AUDIT.md`](file:///d:/FluidCorePDF/fluidcore-platform/docs/POPPLER_MUTEX_LEAK_REMEDIATION_AND_AUDIT.md)
- [`docs/POPPLER_BUILD_INFO.txt`](file:///d:/FluidCorePDF/fluidcore-platform/docs/POPPLER_BUILD_INFO.txt)
