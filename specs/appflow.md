# Xournal++ & FluidCore AppFlow: User Journey & System Execution State Machines

This document specifies the step-by-step lifecycle and state transitions for both the **User Journey Flow (Screen-to-Screen UI interaction)** and the **System Execution Flow (Machine & Service interaction)** for Xournal++ augmented by the **Decoupled C++ Core Engine (`libfluidcore`)**.

---

# Part 1: User Journey Flow (Screen-to-Screen)

```mermaid
stateDiagram-v2
    [*] --> ApplicationLaunch

    state ApplicationLaunch {
        [*] --> CheckCrashRecovery
        CheckCrashRecovery --> CrashDialog: Legacy emergencysave.xopp detected
        CheckCrashRecovery --> LoadTargetDoc: Open .ltproj (WAL auto-replays) or .xopp
        CheckCrashRecovery --> NewBlankDoc: No input file
        CrashDialog --> LoadTargetDoc: Restore / Delete
    }

    ApplicationLaunch --> ProjectDashboard

    state ProjectDashboard {
        DashboardIdle: Recent Projects List / Create New Project
    }

    ProjectDashboard --> MainWorkspace: Open Recent / Create / Import

    state MainWorkspace {
        [*] --> SplitWorkspaceIdle

        state SplitWorkspaceIdle {
            DocumentPane: Left Document Viewport (Poppler / Squeeze)
            WorkspaceCanvas: Right Infinite 2D Workspace (libfluidcore)
        }

        SplitWorkspaceIdle --> TouchPinchSqueeze: Two-Finger Pinch on Document Pane
        SplitWorkspaceIdle --> DesktopSqueeze: Ctrl+Shift+Scroll / Margin Pin Drag
        TouchPinchSqueeze --> SplitWorkspaceIdle: Touch Lifted / Spring Back
        DesktopSqueeze --> SplitWorkspaceIdle: Scroll Stopped / Pin Released

        SplitWorkspaceIdle --> BookmarkPeeking: Shift+Hold or Space+Hold Skim
        BookmarkPeeking --> SplitWorkspaceIdle: Key Released / Return to Pinned Page

        SplitWorkspaceIdle --> DraggingExcerpt: Text / Box Selection Dragged Across Splitter
        DraggingExcerpt --> SplitWorkspaceIdle: Excerpt Card Dropped onto Canvas

        SplitWorkspaceIdle --> NavigateToSource: Tap Excerpt Source Anchor Arrow
        NavigateToSource --> ShowReturnPill: Document Scrolls to Source + Pulse Highlight
        ShowReturnPill --> SplitWorkspaceIdle: Click Floating Return Pill ("Back to Excerpt")

        SplitWorkspaceIdle --> DrawingInkLink: Draw Stroke Between Two Workspace Cards
        DrawingInkLink --> SplitWorkspaceIdle: Stroke Becomes Live Elastic Bezier Link
    }

    MainWorkspace --> PreferencesModal: Menu -> Preferences
    PreferencesModal --> MainWorkspace: Apply / Close

    MainWorkspace --> ExportModal: File -> Export (PDF / DOCX / MD)
    ExportModal --> MainWorkspace: Export Completed
```

---

## 1. Entry Points & Project Launch Scenarios

```mermaid
graph TD
    CLI["CLI Command Line: xournalpp [file.ltproj / file.xopp]"] --> PARSE_OPTS["Parse Command Line Options"]
    DESKTOP["File Manager Double Click (.ltproj)"] --> PARSE_OPTS
    AUTOLOAD["Preferences: Autoload Most Recent Project"] --> PARSE_OPTS

    PARSE_OPTS --> CHECK_EXPORT{"Is CLI Export Mode?"}
    CHECK_EXPORT -->|Yes| HEADLESS["Headless Mode: Export PDF & Exit"]
    CHECK_EXPORT -->|No| GUI_START["GUI Mode: Launch GTK3 Application Loop"]

    GUI_START --> CHECK_EMERGENCY{"Check Recovery Mechanism"}
    CHECK_EMERGENCY -->|Legacy .xopp Crash| RECOVERY_MODAL["Display emergencysave.xopp Modal"]
    CHECK_EMERGENCY -->|.ltproj Loading| OPEN_PROJ["Open .ltproj Bundle via libfluidcore (WAL Replays Automatically)"]
    RECOVERY_MODAL --> OPEN_PROJ
    OPEN_PROJ --> PROJECT_DASHBOARD["Project Dashboard: Recent Projects / Create New"]
    PROJECT_DASHBOARD --> MAIN_WS["Enter Main Dual-Pane Workspace"]
```

1. **`.ltproj` Container Loading**: Opening a project mounts the local directory bundle `Project.ltproj/` (or unpacks `.ltproj.zip` if opening a compressed archive), initializes `libfluidcore::ProjectStore`, automatically replays any uncommitted transactions in the SQLite Write-Ahead Log (`project.db-wal`), and rehydrates the 2D infinite workspace scene graph.
2. **Crash Recovery Separation**: 
   - **Legacy `.xopp` files**: Rely on Xournal++'s legacy `emergencysave.xopp` XML dump mechanism.
   - **Modern `.ltproj` files**: Crash recovery is handled entirely transparently by the SQLite WAL upon database initialization. No recovery modal is shown because WAL replay guarantees sub-500ms bounds automatically.

---

## 2. Main Workspace Layout & Dual-Pane Navigation

```mermaid
graph TB
    subgraph Top Menubar & Dynamic Toolbars
        MENUS["Menu Bar: File | Edit | View | Workspace | Tools | Help"]
        MAIN_BAR["Main Toolbar: Undo, Redo, Save, Export, Split Presets (50-50 / Doc / WS)"]
        TOOL_BAR["Tool Palette: Pen, Highlighter, Eraser, Select, Text, Link, Sticky"]
    end

    subgraph Central Dual-Pane GtkPaned Container
        DOC_PANE["Left: Squeezed Document Viewport (XojPageView / Cairo)"]
        SPLITTER["Draggable Splitter Handle (GtkPaned)"]
        WS_CANVAS["Right: Infinite 2D Workspace (WorkspaceView / Cairo / libfluidcore)"]
    end

    subgraph Collapsible Sidebar Tabs
        THUMBNAIL_TAB["Thumbnail Previews Tab"]
        TOC_TAB["PDF Table of Contents Outline"]
        SEARCH_SLICES_TAB["Search Context Stream Tab (Sequential Slices)"]
        LAYERS_TAB["Layer & Excerpt Manager"]
    end

    MENUS --> DOC_PANE
    MENUS --> WS_CANVAS
    TOOL_BAR --> DOC_PANE
    TOOL_BAR --> WS_CANVAS
    DOC_PANE --- SPLITTER --- WS_CANVAS
    SEARCH_SLICES_TAB -->|Click Search Hit| DOC_PANE
```

---

## 3. Desktop & Touch Interaction Matrix

| User Action | Touch Interaction | Desktop Mouse / Keyboard Interaction | System Response |
|---|---|---|---|
| **Pinch-to-Squeeze** | Two-finger vertical pinch on document pane | `Ctrl + Shift + Mouse Wheel` at cursor Y position | Computes piecewise coordinate map $\mathcal{T}(Y_{doc})$; folds intermediate pages into pleated creases. |
| **Search Squeeze Toggle** | Pinch while search is active | Press `Ctrl + Shift + S` | Collapses non-matching text, displaying hits in sequence. |
| **Margin Fold Pin Drag** | Touch and drag margin pin handle | Click and drag margin pin handle | Adjusts specific fold boundaries interactively. |
| **Bookmark Peeking** | Hold one finger on page, skim with another | Hold `Shift` or `Space` while scrolling | Temporarily inspects distant pages (`beginPeek()`); releasing snaps back (`endPeek()`). |
| **Excerpt Drag & Drop** | Drag selected text/box across splitter | Click-and-drag selection across splitter | Instantiates `ExcerptCardNode` at drop coordinate $(X, Y)$ in `libfluidcore`. |
| **Back-Link Jump** | Tap source arrow on card | Click source arrow on card | Scrolls document to source bounding box with luminous pulse; displays floating return pill in viewport overlay. |
| **Return Pill Click** | Tap floating return pill | Click floating return pill | Glides infinite workspace viewport back to original excerpt card. |

---

# Part 2: System Execution Flow (Machine & Service Interaction)

---

## 1. Application Startup & `libfluidcore` Initialization

```mermaid
sequenceDiagram
    autonumber
    participant Main as main() (src/exe/Xournalpp.cpp)
    participant XMain as XournalMain (src/core/control/XournalMain.cpp)
    participant Ctrl as Control (src/core/control/Control.cpp)
    participant FluidCore as libfluidcore Engine
    participant Win as MainWindow (src/core/gui/MainWindow.cpp)
    participant WSView as WorkspaceView (src/core/gui/workspace/WorkspaceView.cpp)

    Main->>XMain: XournalMain::run(argc, argv)
    XMain->>Ctrl: new Control()
    Ctrl->>FluidCore: Initialize FluidCoreAPI & SQLite WAL Store
    XMain->>Win: new MainWindow(Control)
    Win->>Win: initXournalWidget() (Create GtkPaned dual-viewport)
    Win->>WSView: new WorkspaceView(Control*, FluidCoreAPI*)
    XMain->>Ctrl: openProject(targetLtprojPath)
    Ctrl->>FluidCore: loadProjectDb(project.db) -> Rehydrate Scene Graph
```

---

## 2. Excerpt Drag-and-Drop & Graph Edge Pipeline

```mermaid
sequenceDiagram
    autonumber
    actor User as User
    participant FloatTB as PdfFloatingToolbox
    participant GTKDnD as Drag / GTK3 DND Controller
    participant WSView as WorkspaceView
    participant FluidCore as libfluidcore::WorkspaceModel
    participant SQLite as SQLite WAL (project.db)

    User->>FloatTB: Drag Excerpt Handle / Selection
    FloatTB->>GTKDnD: Prepare ExcerptDropPayload (MIME: "application/x-fluid-excerpt")
    User->>WSView: Drop at Viewport Pixel (px, py)
    WSView->>WSView: screenToWorld(Point(px, py)) -> (wx, wy)
    WSView->>FluidCore: queryNearbyNodes(wx, wy, radius=max(cardW, cardH) + 32pt)
    alt Magnetic Snapping Detected
        FluidCore-->>WSView: Snap Alignment Offset (SnapX, SnapY)
    end
    WSView->>FluidCore: insertNode(ExcerptCardNode(wx, wy, docUuid, page, rect))
    FluidCore->>SQLite: Write INSERT INTO workspace_nodes & source_anchors
    FluidCore-->>WSView: Node Registered (UUID = uuid_str)
    WSView->>WSView: gtk_widget_queue_draw(cardBoundingBox)
```

---

## 3. Dynamic Accordion Squeeze Execution Pipeline

```mermaid
sequenceDiagram
    autonumber
    actor User as User (Touch Pinch / Ctrl+Shift+Scroll / Ctrl+Shift+S)
    participant Input as InputContext / SqueezeController
    participant SqueezeEng as libfluidcore::SqueezeEngine
    participant DocView as XournalView / XojPageView
    participant Cairo as Cairo 2D Pipeline

    User->>Input: Squeeze Input Event (Delta Y / Scroll / Shortcut)
    Input->>SqueezeEng: updateSqueeze(docId, yStart, yEnd, alpha)
    SqueezeEng->>SqueezeEng: Compute Piecewise Coordinate Transform T(Y)
    SqueezeEng-->>DocView: Transform Updated (Dirty Slices)
    DocView->>Cairo: renderSqueezedSlice(cr, pageIndex, docY_start, docY_end, screenY)
    DocView->>Cairo: Draw Procedural Pleated Crease Mesh (Linear Gradient Pattern)
    DocView->>DocView: gtk_widget_queue_draw_area(creaseDamageRect)
```

---

## 4. SQLite WAL Save & Crash-Safety Protocol

```mermaid
sequenceDiagram
    autonumber
    participant Timer as 500ms Debounce Timer / Ctrl+S
    participant Ctrl as Control
    participant FluidCore as libfluidcore::ProjectStore
    participant SQLite as SQLite 3 (project.db + WAL)
    participant Disk as Persistent NVMe/SSD Storage

    Timer->>Ctrl: triggerSave()
    Ctrl->>FluidCore: commitPendingMutations()
    FluidCore->>SQLite: BEGIN IMMEDIATE TRANSACTION
    FluidCore->>SQLite: Flush updated node coordinates, ink strokes & edges
    FluidCore->>SQLite: COMMIT TRANSACTION
    SQLite->>Disk: Write-Ahead Log (WAL) Synchronous Flush
    Note over SQLite,Disk: ACID Guarantee: Sub-500ms bounded loss recovery
```

---

*This concludes the complete AppFlow specification for Xournal++ and libfluidcore.*
