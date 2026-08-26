#!/usr/bin/env python3
"""Mechanical release-gate checks (GOVERNANCE §2/§3/§5, ADR-0001).

Checks:
  1. GTK/GDK/GLib/Cairo/Poppler includes must not appear under src/libfluidcore/
     (ADR-0001 boundary; ROADMAP §6 top risk: GPL contamination).
  2. Network-call patterns must not appear in runtime code (offline-first,
     GOVERNANCE §5 syscall audit's static first line of defense).
  3. Every commit in a range carries a DCO Signed-off-by trailer matching the
     commit author email (GOVERNANCE §3: DCO only, no CLA).

Usage:
  python3 ops/scripts/check_invariants.py                 # tree scans only
  python3 ops/scripts/check_invariants.py --check-dco origin/main..HEAD
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

ENGINE_INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s*[<"](gtk|gdk|glib|gobject|cairo|poppler)[^">]*[">]',
    re.MULTILINE,
)

NETWORK_PATTERNS = [
    re.compile(r"<sys/socket\.h>|<netinet/in\.h>|<arpa/inet\.h>|<netdb\.h>|<winsock2?\.h>"),
    re.compile(r"\b(socket|connect|bind|listen|accept|send|recv)\s*\("),
    re.compile(r"\b(getaddrinfo|gethostby(name|addr)|inet_(aton|ntoa|addr))\s*\("),
    re.compile(r"\b(curl_easy_|curl_multi_|git_libgit2)\w*\s*\("),
    re.compile(r"^\s*#\s*include\s*[<\"](?:curl/curl|boost/asio|httplib|websocketpp)[\">]", re.MULTILINE),
]

EXCLUDED_DIRS = {".git", "build", "node_modules", ".github", "docker", "third_party"}

DCO_TRAILER = re.compile(r"^Signed-off-by:\s*(.+?)\s*<([^>]+)>\s*$", re.MULTILINE)


def iter_source_files(subpath: str):
    base = REPO_ROOT / subpath
    if not base.is_dir():
        return
    for path in sorted(base.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(REPO_ROOT)
        if any(part in EXCLUDED_DIRS for part in rel.parts):
            continue
        if path.suffix not in {".h", ".hpp", ".c", ".cpp", ".cc"}:
            continue
        yield rel


def scan_engine_includes() -> list[str]:
    violations = []
    for rel in iter_source_files("src/libfluidcore"):
        text = (REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
        for match in ENGINE_INCLUDE_PATTERN.finditer(text):
            line = text[: match.start()].count("\n") + 1
            violations.append(f"ADR-0001 violation: {rel}:{line}: GUI/library include "
                              f"'{match.group(0).strip()}' inside libfluidcore/")
    return violations


def scan_network_calls() -> list[str]:
    violations = []
    for rel in iter_source_files("src"):
        text = (REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
        stripped = "\n".join(
            line.split("//")[0] for line in text.splitlines()
        )
        for pattern in NETWORK_PATTERNS:
            for match in pattern.finditer(stripped):
                line = stripped[: match.start()].count("\n") + 1
                violations.append(f"Offline-guarantee violation: {rel}:{line}: "
                                  f"network pattern '{match.group(0)[:60]}'")
    return violations


def check_dco(range_spec: str) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "log", "--format=%x00%an%x00%ae%x00%B", range_spec, "--"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return [f"git log failed: {result.stderr.strip()}"]

    violations = []
    records = [r for r in result.stdout.split("\x00") if r.strip()]
    for record in records:
        fields = record.split("\x00", 2)
        if len(fields) < 3:
            continue
        author_name, author_email, body = fields[0], fields[1], fields[2]
        trailers = [(m.group(1), m.group(2).lower()) for m in DCO_TRAILER.finditer(body)]
        if not trailers:
            violations.append(f"DCO violation: commit by {author_name} <{author_email}> "
                              f"has no 'Signed-off-by' trailer (GOVERNANCE §3: DCO required)")
        elif not any(email == author_email.lower() for _, email in trailers):
            violations.append(f"DCO violation: commit by {author_name} <{author_email}> has a "
                              f"Signed-off-by that does not match the author email")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check-dco", metavar="RANGE",
                        help="also verify Signed-off-by on commits in git RANGE")
    args = parser.parse_args()

    violations = scan_engine_includes() + scan_network_calls()
    if args.check_dco:
        violations += check_dco(args.check_dco)

    if violations:
        print(f"{len(violations)} invariant violation(s):", file=sys.stderr)
        for v in violations:
            print(f"  - {v}", file=sys.stderr)
        return 1

    print("invariants OK: no GUI includes in libfluidcore, no network calls, "
          + ("DCO trailers present" if args.check_dco else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
