# Specs Workspace

Last updated: 2026-09-05

## What this is for
How the system works: interaction design, architecture, execution flows, and codebase mapping. Read the file the routing table points to — these are long documents; do not load all of them.

## Files
| File | Contents | Load when |
|------|----------|-----------|
| `features.md` | Full fluid synthesis feature & interaction spec (squeeze, excerpts, links, stacks) | Designing/verifying any user-facing behavior |
| `system-architecture.md` | Deep dive: GTK/Cairo/Poppler/SQLite + libfluidcore layers, MVC split | Architecture questions, module boundaries |
| `appflow.md` | User journey + system execution state machines | Implementing flows, debugging lifecycle issues |
| `integration.md` | Xournal++ integration plan | Touching upstream files, rebasing strategy |
| `file-function-map.md` | Source file/class responsibility map incl. libfluidcore layout | Locating where code belongs |
| `new-features-backlog.md` | Novel features beyond core specification | Idea evaluation only — nothing here blocks v1.0 |

## Process
1. Feature work starts from `features.md` section reference (cite it in PRs)
2. Implementation must respect `system-architecture.md` module boundaries — especially: engine logic → `libfluidcore`, GTK glue stays in `src/app`
3. If implementation deviates from spec, update the spec in the same PR

## What good looks like
A new contributor can read one spec file plus src/CONTEXT.md and know exactly which file to create or edit.

## Avoid
- Loading every spec at once — token waste; use the routing table
- Specifying UI behavior that has no mouse/keyboard equivalent (MVP D2 requires parity)
