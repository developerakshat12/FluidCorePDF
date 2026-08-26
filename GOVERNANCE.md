# Governance

## 1. Roles
| Role | Powers | Attainment |
| :--- | :--- | :--- |
| Contributor | PRs, issues, discussions | Anyone |
| Reviewer | Approve non-perf-gated PRs | 5 merged PRs + maintainer invite |
| Maintainer (per-area) | Merge rights, milestone planning in their area (engine / GUI / storage) | Sustained contribution over ≥ 2 milestones, consensus of existing maintainers |
| Lead Maintainers (2–3) | License decisions, releases, final arbitration | Founding team; succession by supermajority of maintainers |

## 2. Decision Making
- **Lazy consensus**: proposals in GitHub Discussions pass after 7 days with no unresolved objection.
- **Perf-gated areas** (`squeeze/`, `workspace/RTree*`, render cache): require two approvals and a benchmark artifact attached to the PR.
- **ADR-required changes**: license, MVP scope (MVP-SPEC.md), data format `.ltproj` schema — need Lead Maintainer sign-off.

## 3. Licensing Strategy
- Default project license: **GPL-2.0-or-later** (inherited from Xournal++).
- `libfluidcore/` is kept free of GPL-only dependencies so it *may* be relicensed later (candidate: LGPL-2.1-or-later or Apache-2.0 for the engine alone) to enable non-GPL frontend plugins. Any relicensing requires Lead Maintainer unanimity and contributor consent per CLA-lite policy (DCO only; no CLA).

## 4. Data Format Commitment
`.ltproj` is a documented, versioned SQLite schema. v1.0 promises:
- Forward readability: vN apps can read files written by any older version.
- A public `docs/specs/ltspec.md` schema document ships with every release.
- Breaking schema changes bump the major version and ship a migration tool.

## 5. Security & Offline Guarantee
- CI runs a syscall/network audit asserting zero runtime network access.
- Reports of security issues → private security advisory on GitHub (not public issues); 90-day disclosure window.

## 6. Code of Conduct Enforcement
Maintainers may warn, mute, or ban. Appeals go to Lead Maintainers. Repeated violations across accounts are treated as one record.
