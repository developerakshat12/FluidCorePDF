# Planning Workspace

Last updated: 2026-09-04

## What this is for
Product-level truth: what we build, for whom, in what order, and what "done" means. Strategy and scope live here; implementation detail lives in `/specs`.

## Files
| File | Contents |
|------|----------|
| `PRD.md` | Problem, personas (Sarah/Aris/Elena/Marcus), requirements, success criteria |
| `TRD.md` | Technical objectives, subsystem mapping, perf targets, engineering challenges |
| `ROADMAP.md` | Milestones M0–M6, exit criteria, perf budgets (§5 = merge gates), risks |
| `MVP-SPEC.md` | Contractual v1.0 scope: must-haves A1–E3 with acceptance criteria; out-of-scope list |
| `roadmap.yaml` | Machine-readable milestone/status data — update when a milestone item changes state |
| `backlog.yaml` | Post-1.0 prioritized backlog (P1–P3) |
| `decisions/` | ADRs — binding until superseded. Template included |

## Process
1. New scope idea → check MVP-SPEC §3 (out-of-scope) first
2. In-scope work → confirm it maps to a ROADMAP milestone; if not, it goes to `backlog.yaml`
3. Design decisions affecting architecture, license, or `.ltproj` schema → write an ADR before coding
4. When an item's status changes, update BOTH `ROADMAP.md` checkbox and `roadmap.yaml`

## What good looks like
Every feature traceable: persona → PRD requirement → MVP acceptance criterion → milestone → spec → code.

## Avoid
- Adding features not in MVP-SPEC or backlog without an ADR ("scope creep toward Notion clone" is a tracked risk)
- Changing MVP scope without Lead Maintainer sign-off (GOVERNANCE §2)
