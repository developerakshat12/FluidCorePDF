# Skill: Spec-to-Code (Milestone Implementation)

## Purpose
Convert one roadmap item into a complete vertical-slice PR.

## Procedure
1. Pick item from `planning/roadmap.yaml` (status: pending, earliest milestone first); flip to `in_progress` in BOTH yaml and ROADMAP.md
2. Read chain: MVP-SPEC acceptance criterion → relevant section in `/specs/features.md` → architecture notes in `system-architecture.md`
3. Slice vertically: engine logic + headless test + frontend wiring + persistence — not horizontal layers
4. Route subtasks through cpp-core-dev / gtk-frontend skills as appropriate
5. If milestone has perf gates → run perf-gate skill
6. On completion: tick ROADMAP checkbox, set roadmap.yaml status, note demo gate evidence

## Done criteria
- Acceptance criterion demonstrably met (demo gate recorded)
- Docs touched: spec + file-function-map + roadmap.yaml consistent
