# ICM Method Summary (this repo's meta-architecture)

Source: `../ICM/` guides + paper 2603.16021v2. This structure follows it.

## The three layers
1. **Layer 1 — Map**: root `CLAUDE.md`. Routing table only: task → workspace → files to read → skill. Kept short (~50 lines); no project briefs inside it.
2. **Layer 2 — Rooms**: one `CONTEXT.md` per workspace describing what happens there, its process, files, and quality bar. Loaded ONLY when working in that room. Each carries a "Last updated" stamp.
3. **Layer 3 — Tools**: `skills/<name>/SKILL.md`, wired per-workspace through the routing table's Skills column. Never loaded globally.

## Operating rules adopted here
- Route before reading — never load every document
- Context files describe the WORK (80%), not AI behavior (20%)
- Naming conventions replace databases (see CLAUDE.md)
- Workspaces = mental-mode boundaries; subfolders within a workspace, not new workspaces, when unsure
- Context files are living documents: update on every project change
- Machine-readable state lives in YAML (`project.yaml`, `roadmap.yaml`, `backlog.yaml`) so agents can query status without parsing prose
