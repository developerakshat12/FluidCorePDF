#!/usr/bin/env python3
"""Mechanical perf-budget verifier (ROADMAP §5, ops/CONTEXT.md standing gates).

Validates a bench-<area>.md artifact attached to a perf-gated PR:
  1. Every required metric section is present.
  2. The claimed value satisfies the budget.

Usage:
  python3 ops/scripts/check_budgets.py benchmarks/bench-squeeze.md
  python3 ops/scripts/check_budgets.py --self-test
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

# metric key -> (budget value, direction, display unit, section regex)
# direction: "max" means claimed <= budget; "min" means claimed >= budget.
BUDGETS: dict[str, tuple[float, str, str, re.Pattern[str]]] = {
    "inking_latency_ms": (
        20.0,
        "max",
        "ms",
        re.compile(r"inking[^\n]*?latency[^\n]*?:\s*([0-9.]+)\s*ms", re.IGNORECASE),
    ),
    "squeeze_fps_1080p": (
        30.0,
        "min",
        "fps",
        re.compile(r"squeeze[^\n]*?(?:fps|frame[s]?[^\n]*?second)[^\n]*?:\s*([0-9.]+)",
                   re.IGNORECASE),
    ),
    "spatial_query_p99_ms": (
        1.0,
        "max",
        "ms",
        re.compile(r"spatial[^\n]*?p99[^\n]*?:\s*([0-9.]+)\s*ms", re.IGNORECASE),
    ),
    "cold_load_50pdf_s": (
        8.0,
        "max",
        "s",
        re.compile(r"cold[^\n]*?load[^\n]*?:\s*([0-9.]+)\s*s", re.IGNORECASE),
    ),
    "ram_working_set_gb": (
        1.2,
        "max",
        "GB",
        re.compile(r"ram[^\n]*?(?:working[^\n]*?set)?[^\n]*?:\s*([0-9.]+)\s*(?:GB|gb)",
                   re.IGNORECASE),
    ),
}

REQUIRED_BENCH_SECTIONS = ["Machine specs", "Environment", "Results"]


def check_bench(path: Path) -> list[str]:
    violations: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")

    for section in REQUIRED_BENCH_SECTIONS:
        if section.lower() not in text.lower():
            violations.append(f"{path.name}: missing required section '{section}'")

    for key, (budget, direction, unit, pattern) in BUDGETS.items():
        match = pattern.search(text)
        if not match:
            violations.append(f"{path.name}: no claim found for '{key}' (expected "
                              f"a '...: <number> {unit}' line)")
            continue
        claimed = float(match.group(1))
        if direction == "max" and claimed > budget:
            violations.append(f"{path.name}: {key} = {claimed} {unit} exceeds budget "
                              f"{budget} {unit} (ROADMAP §5)")
        elif direction == "min" and claimed < budget:
            violations.append(f"{path.name}: {key} = {claimed} {unit} below budget "
                              f"{budget} {unit} (ROADMAP §5)")

    return violations


def self_test() -> int:
    fixtures = {
        "ok.md": (
            "# bench-squeeze\n\n## Machine specs\nx86_64, 8 cores\n\n"
            "## Environment\nGCC 12, RelWithDebInfo\n\n## Results\n"
            "squeeze fps: 42.5\ninking latency: 12 ms\n"
            "spatial p99: 0.4 ms\ncold load: 5.2 s\nram working set: 0.9 GB\n"
        ),
        "over_budget.md": (
            "# bench-squeeze\n\n## Machine specs\nx86_64\n\n## Environment\nGCC 12\n\n"
            "## Results\nsqueeze fps: 24.0\ninking latency: 12 ms\n"
            "spatial p99: 0.4 ms\ncold load: 5.2 s\nram working set: 0.9 GB\n"
        ),
        "missing_metric.md": (
            "# bench-squeeze\n\n## Machine specs\nx86_64\n\n## Environment\nGCC 12\n\n"
            "## Results\ninking latency: 12 ms\n"
        ),
    }
    import tempfile

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        for name, content in fixtures.items():
            f = tmpdir / name
            f.write_text(content, encoding="utf-8")
        ok = not check_bench(tmpdir / "ok.md")
        over = bool(check_bench(tmpdir / "over_budget.md"))
        missing = any("no claim found" in v
                      for v in check_bench(tmpdir / "missing_metric.md"))
        if not ok:
            print("self-test FAIL: clean fixture reported violations", file=sys.stderr)
            failures += 1
        if not over:
            print("self-test FAIL: over-budget fixture not flagged", file=sys.stderr)
            failures += 1
        if not missing:
            print("self-test FAIL: missing metric not flagged", file=sys.stderr)
            failures += 1

    if failures == 0:
        print("check_budgets self-test: all cases passed")
        return 0
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("bench", nargs="?", help="path to bench-<area>.md artifact")
    parser.add_argument("--self-test", action="store_true", help="run built-in fixtures")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.bench:
        parser.error("a bench artifact path is required (or use --self-test)")

    path = Path(args.bench)
    if not path.is_file():
        print(f"bench artifact not found: {path}", file=sys.stderr)
        return 1

    violations = check_bench(path)
    if violations:
        print(f"{len(violations)} budget violation(s):", file=sys.stderr)
        for v in violations:
            print(f"  - {v}", file=sys.stderr)
        return 1

    print(f"{path.name}: within ROADMAP §5 budgets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
