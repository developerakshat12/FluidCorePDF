# MVP Specification (Contractual Scope)
## v0.9 → v1.0 Minimum Viable Product

> This document defines the **contractual MVP boundary**. Features listed here must ship in v1.0. Everything else lives in [ROADMAP.md §4](ROADMAP.md) or `newfeatures.md`. Changes to this file require a milestone-exit review.

---

## 1. MVP Definition Statement

The MVP is the smallest product that lets a knowledge worker complete one full **active-reading synthesis loop, 100% offline**:

> Open 1–N PDFs → read with fluid squeeze → extract excerpts to an infinite workspace → connect and stack them → search everything → persist and reopen without data loss.

## 2. Must-Have (v1.0 Blocking)

### 2.1 Document Handling
| # | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| A1 | Load multi-document projects | ≥ 10 PDFs / 1000 pages open simultaneously; per-doc tabs |
| A2 | Continuous scroll + zoom (10%–400%) | No rasterization artifacts at native resolution; ≤ 300 ms page swap p95 |
| A3 | Text selection & copy | Preserves reading order; includes page number on paste |
| A4 | Full-text search per document + project-wide | FTS5-backed; results < 500 ms on 5,000-page project |

### 2.2 Fluid Reading
| # | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| B1 | Accordion squeeze via two-finger pinch | Distant sections touch; release restores flow; no text distortion |
| B2 | Desktop squeeze: `Ctrl+Shift+Scroll` + margin fold pins | Parity with touch behavior |
| B3 | Search-squeeze mode | Hits remain expanded, intermediate pages collapsed |
| B4 | ≥ 30 FPS during squeeze interaction | Mid-range reference hardware (see ROADMAP §5) |

### 2.3 Workspace & Excerpts
| # | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| C1 | Infinite 2D workspace canvas | Pan/zoom unlimited bounds; R-tree hit-testing |
| C2 | Drag-out excerpts (text + image regions) | Excerpt renders within 200 ms of drop |
| C3 | Bi-directional anchors | Click excerpt → source location; `ReturnAnchorPill` returns |
| C4 | Ink connectors between nodes | Stroke endpoints on two cards create persistent `GraphEdge` |
| C5 | Card stacking | Drop within 16 pt proximity snaps into collapsible stack |

### 2.4 Annotation & Input
| # | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| D1 | Pen/highlighter ink with stabilizer | ≤ 20 ms latency; palm rejection on supported digitizers |
| D2 | Mouse/keyboard equivalents for every touch gesture | Full operation possible without touchscreen |

### 2.5 Persistence
| # | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| E1 | `.ltproj` bundle: SQLite WAL storage | Auto-save ≤ 5 s after change; atomic commit |
| E2 | Crash recovery | `kill -9` mid-session loses ≤ last autosave interval; WAL replays cleanly |
| E3 | Export: flattened annotated PDF + Markdown outline | Round-trip opens in Acrobat/most Markdown viewers |

### 2.6 Platform & Distribution
- Linux first-class: Flatpak, AppImage, .deb
- Windows: functional beta build (MSYS2/GTK)
- Zero network calls at runtime (verified by CI syscall audit)

## 3. Explicitly Out of Scope for v1.0

| Deferred Item | Destination |
| :--- | :--- |
| macOS port | Post-1.0 P1 |
| Handwriting OCR | Post-1.0 P1 |
| Cloud sync / accounts | Never (violates core tenet) |
| Plugin API | Post-1.0 P2 |
| EPUB/DOCX import | Post-1.0 P3 |
| Collaboration/CRDT | Post-1.0 P3 |
| Mobile/tablet builds | Unscheduled |

## 4. MVP Success Metrics

| Metric | Target (90 days post-launch) |
| :--- | :--- |
| Weekly active installs (Flathub + GitHub) | ≥ 2,000 |
| Median session length | ≥ 25 min (indicates real reading use) |
| Project save/load success rate | ≥ 99.9% |
| Crash-free sessions | ≥ 99.5% |
| GitHub contributors (merged PRs) | ≥ 15 |
