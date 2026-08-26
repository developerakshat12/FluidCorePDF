# Skill: Perf Gate Verification

## Purpose
Prove a PR meets ROADMAP §5 budgets before merge (mandatory for squeeze/, RTree, render cache paths).

## Budgets
inking ≤ 20 ms · squeeze ≥ 30 FPS @1080p · spatial query p99 ≤ 1 ms @10⁵ items · cold load 50 PDFs ≤ 8 s · RAM ≤ 1.2 GB

## Procedure
1. Identify affected budget(s)
2. Build Release with `-DCMAKE_BUILD_TYPE=Release -DENABLE_BENCHMARKS=ON`
3. Run matching harness from `/ops/benchmarks`; record: machine specs, numbers vs. budget, delta vs. main branch
4. Write artifact `ops/benchmarks/bench-<area>.md` (template: Context / Method / Numbers / Verdict)
5. Over budget? STOP — do not merge. Open an ADR proposing either optimization plan or documented trade-off

## Done criteria
- Artifact attached to PR with pass/fail verdict
- Two approvals obtained for gated areas (GOVERNANCE §2)
