# ADR-0001: libfluidcore GTK Boundary & License Hygiene

Date: 2026-08-26 · Status: Accepted

## Context
The project extends Xournal++ (GPL-2.0-or-later) but isolates its novel engine into `libfluidcore/`. GOVERNANCE §3 wants the option to relicense the engine alone (e.g., LGPL/Apache) to enable non-GPL frontend plugins (backlog: plugin-api).

## Decision
1. `libfluidcore/` is a standalone C++20 CMake target that MUST NOT include GTK/GDK headers or link GPL-only symbols.
2. All GUI coupling lives in `src/app` behind `FluidCoreAPI.h`.
3. Engine tests are headless (`ctest`, no display).
4. Directory hygiene enforced from M0 via CI include-check, not retrofitted.

## Consequences
- Plugin ecosystem remains possible without rewrite.
- Slight duplication where Xournal++ utilities would be convenient — engine re-implements small helpers instead of linking.
- Any violation blocks merge; CI include-scan of `libfluidcore/**` for gtk/gdk headers.
