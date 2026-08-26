# ADR-0002: Bootstrap Authority & FluidCoreAPI.h Generation

Date: 2026-08-26 · Status: Accepted (Provisional — bootstrap period)

## Context

M0 begins from a governance vacuum: GOVERNANCE §1 attainment chains (Reviewer →
Maintainer → Lead Maintainer) require merged PRs and existing maintainers, none of
whom exist on day one. Meanwhile three §2 mechanisms gate the very first artifacts:

- ADR-required changes (license, MVP scope, `.ltproj` schema) need **Lead
  Maintainer sign-off**.
- Perf-gated areas (`squeeze/`, …) need **two approvals + benchmark artifact**.
- Lazy consensus takes **7 days** — incompatible with a same-day bootstrap.

Separately, the engine/frontend boundary contract (`FluidCoreAPI.h`, TRD §4.1) is a
merge-contention point: every parallel implementation task compiles against it, so
it must be generated once, upfront, by a single author.

## Decision

1. **Bootstrap authority:** the operator running this project acts as *acting Lead
   Maintainer* for the bootstrap period. During this period:
   - The acting Lead Maintainer signs off on ADR-required changes and may approve
     perf-gated PRs as both required approvals (with the benchmark-artifact
     requirement still enforced).
   - This authority is provisional and expires when real maintainers exist per
     GOVERNANCE §1; ratification is recorded here rather than silently assumed.
2. **FluidCoreAPI.h is generated once** from TRD §4.1 verbatim method signatures,
   with minimal geometry-only value-type stubs. Supporting type field choices are
   provisional until first external consumer ships.
3. **Persistence surface is signature-only:** `openProject()`/`saveProject()` are
   declared but must be implemented as no-op stubs until M5. No `.ltproj` DDL ships
   before `docs/specs/ltspec.md` exists (GOVERNANCE §4). This defers the
   schema-locking decision along the milestone order already fixed in ROADMAP §3.

## Alternatives Considered

- *Full GOVERNANCE compliance from day one:* impossible — no eligible approvers
  exist; every PR would stall or violate §2.
- *Letting parallel agents draft competing FluidCoreAPI.h versions:* merge
  contention on the one header everyone compiles against.
- *Committing real `.ltproj` DDL now to "save time":* triggers Lead Maintainer
  sign-off for schema changes and breaks the §4 ltspec ship-with-release promise
  before v1.0 exists. Also contradicts M5's own placement of WAL finalization.

## Consequences

- Day-1 PRs have a legitimate merge path; the swarm cannot stall in review.
- The benchmark-artifact requirement stays mechanical (`check_budgets.py`
  output attached to PR) even though approvals are consolidated.
- When the first non-founding maintainer qualifies, this ADR must be revisited and
  either ratified with named maintainers or superseded.
- Any change to `FluidCoreAPI.h` after first consumer ships requires an ADR touch;
  until then, edits are cheap and expected during M0–M2.
