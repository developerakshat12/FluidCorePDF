# Security Policy

## Scope

FluidCore is an **offline-first** application. It makes no network calls at
runtime, and CI asserts zero runtime network access ([GOVERNANCE.md §5](GOVERNANCE.md)).
The security surface is therefore local: file parsing, persistence, and rendering
of untrusted documents.

In scope:

- **PDF parsing.** Poppler is the primary attack surface. Malformed or
  adversarial PDFs are the most likely route to a memory-safety issue.
- **`.ltproj` project files.** A documented, versioned SQLite schema
  ([GOVERNANCE.md §4](GOVERNANCE.md)) opened from an untrusted source.
- **`.xopp` companion files**, which are XML parsed on load and save.
- **Excerpt tile rendering** and the Cairo rendering paths, including
  font handling — see the known Cairo COLR crash class in
  [ops/CONTEXT.md §4](ops/CONTEXT.md).
- **Local persistence**, including recovery after an unclean shutdown.

Out of scope:

- Vulnerabilities in upstream Poppler, GTK, Cairo, or SQLite that are fixed
  upstream. We will coordinate disclosure, but the fix belongs upstream.
- Reports requiring the user to already have local code execution.
- Missing hardening with no demonstrated impact.

## Reporting a Vulnerability

**Do not open a public issue.** Use **GitHub's private vulnerability reporting**:
on this repository, go to `Security` → `Report a vulnerability`.

This opens a private advisory visible only to the maintainers, and satisfies the
process committed to in [GOVERNANCE.md §5](GOVERNANCE.md).

Please include:

- A description of the issue and its impact
- Steps to reproduce, ideally with a minimal PDF or `.ltproj` sample
- Your platform, GPU/driver, and the digitizer model if the issue is
  input-related
- Any proof-of-concept code

If private advisory reporting is unavailable to you, open a regular issue that
says only *"security report, please open a private channel"* — no technical
detail — and a maintainer will arrange a private channel.

## Disclosure

- **Acknowledgement**: within 3 business days.
- **Assessment**: within 10 business days, including a severity judgement and
  whether we accept the report.
- **Fix target**: guided by severity. Critical and high issues are prioritised
  ahead of scheduled milestone work.
- **Disclosure**: coordinated with you after a fix ships, under a **90-day
  disclosure window** from the initial report ([GOVERNANCE.md §5](GOVERNANCE.md)).
  We will not publish before the fix is available unless you ask us to, or the
  issue is being actively exploited.

We ask that you not publicly disclose the issue until that window closes, and
we will credit you in the release notes and advisory unless you prefer
otherwise.

## Supported Versions

Security fixes ship on the current release line. Because FluidCore is
pre-release (v1.1.x, with v1.0 not yet tagged), fixes are applied to `main` and
released rather than backported to older tags.

| Version | Supported |
| :--- | :--- |
| `main` | Yes |
| Latest tagged release | Yes |
| Older tagged releases | No |

## Hardening Notes

Known constraints that affect how we assess reports:

- **Cairo is pinned.** Upstream Cairo 1.18.6+ aborts in
  `cairo-colr-glyph-render.c:1168` when the DirectWrite backend queries system
  fonts with COLR colour tables, killing the app on launch with `0xC0000409`.
  Both CI workflows pin Cairo to `1.18.4-4`. A report that a newer Cairo crashes
  on a COLR font is a known issue, not a new vulnerability.
- **The GTK/Poppler boundary is a hard architectural rule.**
  `libfluidcore/` must never include GTK headers (ADR-0001). This keeps the
  engine headlessly testable and independently auditable, so a parser-level
  issue in the engine is far cheaper to fix than one in the frontend.
