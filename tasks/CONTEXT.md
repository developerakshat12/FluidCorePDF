# Tasks (Agent Task Cards)

Machine-readable task cards for autonomous agents. One YAML file per task,
named `TASK-<milestone>.<slice>-<slug>.yaml`.

Card schema:
- `id`, `title`, `milestone` — identity
- `workspace` — repo directory the agent works in
- `specs` — documents to read before writing code
- `dependencies` — other task ids that must land first (empty = parallelizable)
- `deliverables` — concrete files/artifacts expected
- `acceptance_checks` — verifiable done-criteria; ALL must pass
- `constraints` — hard rules the agent may not violate

Agents: load `skills/m0-bootstrap/SKILL.md` before starting any card.
