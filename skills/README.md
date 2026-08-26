# Skills Registry (Layer 3)

Skills are wired into workspaces via the routing table in `/CLAUDE.md`. Load only the skill routed for the current task — never all of them.

| Skill | Wired into | Use when |
|-------|-----------|----------|
| [cpp-core-dev](cpp-core-dev/SKILL.md) | /src (engine paths) | Writing/changing `libfluidcore` C++ |
| [gtk-frontend](gtk-frontend/SKILL.md) | /src/app | GTK/Cairo UI work |
| [perf-gate](perf-gate/SKILL.md) | /ops | Verifying or benchmarking perf budgets |
| [spec-to-code](spec-to-code/SKILL.md) | milestone implementation tasks | Turning a spec'd roadmap item into a vertical-slice PR |
| [m0-bootstrap](m0-bootstrap/SKILL.md) | tasks/TASK-*.yaml cards | Autonomous agent starting any task card (mandatory first read) |

Adding a skill: create `<name>/SKILL.md` with Purpose / Inputs / Procedure / Done-criteria, then add one row to the CLAUDE.md routing table.
