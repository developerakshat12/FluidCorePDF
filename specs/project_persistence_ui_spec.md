# Project & Workspace Persistence UI Specification

## 1. Overview & Objectives

This specification defines the graphical user interface architecture for multi-document project management and workspace canvas persistence in the FluidCore desktop client. It eliminates the requirement for CLI arguments, allowing users to:
1. Create new projects (`Ctrl+N`) with unified dirty-state protection and action resumption.
2. Ingest PDFs dynamically via native file choosers (`Ctrl+O`) with deadlock-free teardown concurrency.
3. Open existing `.ltproj` bundles (`Ctrl+Shift+O`) with structural and schema validation.
4. Save the active workspace canvas, excerpt cards, stacks, tags, links, multi-doc sources, and inking directly through the UI with full ACID durability and partial-copy rollback guarantees (`Ctrl+S`, `Ctrl+Shift+S`).

---

## 2. HeaderBar Visual Design & Layout

The top application chrome utilizes a modern GNOME-style `GtkHeaderBar` integrated directly into the window titlebar via `gtk_window_set_titlebar()`.

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ [🗁 New] [📂 Open ▾] [💾 Save] [Save As...]    FluidCore — Legal Brief.ltproj [● Saved]   [📤 Export] [🗕 🗖 ✕] │
└────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 CSS Design System Tokens

```css
/* HeaderBar Container */
.fc-header-bar {
  background-color: #0f172a; /* Deep slate background */
  border-bottom: 1px solid #1e293b;
  min-height: 46px;
  padding: 0 12px;
}

/* Header Action Buttons */
.fc-header-btn {
  background-color: #1e293b;
  color: #f8fafc;
  border: 1px solid #334155;
  border-radius: 6px;
  padding: 5px 12px;
  font-size: 13px;
  font-weight: 500;
  transition: all 120ms ease-in-out;
}

.fc-header-btn:hover {
  background-color: #334155;
  border-color: #475569;
  color: #ffffff;
}

.fc-header-btn:active {
  background-color: #2563eb;
  border-color: #1d4ed8;
}

.fc-header-btn-primary {
  background-color: #2563eb;
  border-color: #1d4ed8;
  color: #ffffff;
}

.fc-header-btn-primary:hover {
  background-color: #1d4ed8;
  border-color: #1e40af;
}

/* Save Status Indicator Pill */
.fc-save-status-pill {
  border-radius: 12px;
  padding: 3px 10px;
  font-size: 11px;
  font-weight: 600;
  margin-left: 8px;
}

.fc-save-status-saved {
  background-color: rgba(16, 185, 129, 0.12);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.25);
}

.fc-save-status-unsaved {
  background-color: rgba(245, 158, 11, 0.12);
  color: #f59e0b;
  border: 1px solid rgba(245, 158, 11, 0.25);
}

.fc-save-status-failed {
  background-color: rgba(239, 68, 68, 0.12);
  color: #ef4444;
  border: 1px solid rgba(239, 68, 68, 0.25);
}
```

---

## 3. Action Mapping & Keyboard Shortcuts

| Action | Accelerator | UI Trigger | Handler Behavior |
| :--- | :--- | :--- | :--- |
| **New Project** | `Ctrl+N` | Header button `[🗁 New]` | Guards unified dirty state with single prompt. On confirmed save/discard, resets workspace model, clears DocumentPane, resets undo stacks, sets title to "Untitled Project". |
| **Open PDF...** | `Ctrl+O` | Menu item in `[📂 Open ▾]` | Guards unified dirty state if replacing document. Opens native file dialog for `*.pdf`, hot-loads document into DocumentPane, registers in `PdfDocumentService` without app restart. |
| **Open Project...** | `Ctrl+Shift+O` | Menu item in `[📂 Open ▾]` | Guards unified dirty state with single prompt. Opens native folder chooser for `.ltproj`, validates `project.db` and schema, rehydrates workspace nodes, stacks, tags, edges, and loads the bundle's primary PDF. |
| **Save Project** | `Ctrl+S` | Header button `[💾 Save]` | Saves `project.db`, workspace cards, stacks, tags, edges, and active document `.xopp` ink. Redirects to "Save As" if no project path is set. Displays error dialog if I/O fails. |
| **Save Project As...** | `Ctrl+Shift+S` | Header button `[Save As...]` | Opens native folder chooser to create `.ltproj` bundle, enforces `.ltproj` extension, copies ALL registered PDFs and `.xopp` files, rolls back on copy failure, re-points canonical handles to bundle, executes atomic commit. |
| **Export** | `Ctrl+E` | Header button `[📤 Export]` | Opens existing `ExportDialog` (annotated PDF vector flattening / Markdown outline synthesis). |

---

## 4. Architectural Rules & Invariants

### 4.1 Unified Dirty-State Protection & Action Resumption
Every workflow that resets or replaces current workspace memory (`New Project`, `Open Project`, or replacing document) executes a unified dirty check:
- `isSessionDirty() = (workspaceView && workspaceView->undoStack().canUndo()) || (documentPane && documentPane->undoStack().canUndo()) || isManualDirty;`
- Exactly **one** modal dialog is presented: *"You have unsaved changes. Save changes before proceeding?"*
  - `[Save Changes]`:
    - If project has an established bundle path: calls `engine.saveProject()` and `pane->saveAnnotations()`. On success, immediately executes the pending action.
    - If project has NO path yet (Save As required): queues the requested action into `m_pendingAction` (`NewProject`, `OpenPdf`, `OpenProject`). Invokes the native folder chooser for Save As. Upon successful save, the pending action automatically resumes and completes. If Save As is cancelled or fails, the pending action is discarded, preserving the current session without executing the destructive action.
  - `[Don't Save]`: Discards unsaved modifications and continues with the requested action.
  - `[Cancel]`: Aborts the action; nothing in memory is touched.

### 4.2 Multi-Document Bundle Ingestion, Rollback & Canonical Switch
On `Save Project As...`:
1. The destination directory `<BundleName>.ltproj/` is established.
2. The user input is checked; if `.ltproj` extension is omitted, `.ltproj` is automatically appended.
3. The bundle structure (`/documents/`, `/assets/`, `/cache/`) is created.
4. **All** documents currently tracked in `PdfDocumentService` (not just the one currently in view) are copied into `<BundleName>.ltproj/documents/<doc_id>.pdf`.
5. Companion `.xopp` ink files are copied into `<BundleName>.ltproj/documents/<doc_id>.xopp`.
6. **Partial Failure Rollback**: If copying any document fails (disk full, permission denied), the partially created `<bundle>.ltproj` directory is completely removed (`std::filesystem::remove_all`), an error modal is shown, and the project state is left uncommitted. On retry, the user can choose the same folder or a new location.
7. Each document is registered into `ProjectStore` with its normalized relative bundle path.
8. **Canonical Switch**: Live handles in `PdfDocumentService` and `DocumentPane` are immediately switched to the newly copied files inside the bundle. The bundle copy becomes the one true source of truth for the ongoing session.
9. SQLite transactions are committed and `metadata.json` written atomically.

### 4.3 Folder-Content & Schema Validation on Open
When the user picks a folder via `GtkFileChooserNative`:
1. Verify `std::filesystem::exists(folderPath / "project.db")`. If missing:
   - Display error modal: *"The selected folder is not a valid FluidCore project (missing project.db)."*
   - Abort opening; leave existing workspace intact.
2. Read `metadata.json` and inspect `schema_version`:
   - If `schema_version > 1` (newer unsupported schema):
     - Display error modal: *"This project was created by a newer version of FluidCore (schema vX, supported v1). Please update your application."*
     - Abort opening; leave existing workspace intact.

### 4.4 Non-Blocking Teardown Concurrency (Deadlock Prevention)
When hot-reloading a PDF in `DocumentPane`:
1. The main thread calls `pdfDocService->cancelDocumentRequests(oldDocId)` to set cancellation state.
2. Background workers in `ExcerptTileCache::asyncWorkerFunc` check `isDocumentCancelled(docId)` before beginning work, during render, and before posting `g_idle_add(onRenderCompletedIdle)`.
3. The main thread never blocks on `m_workerPopplerMutex`. Background document instances are owned by `PdfDocumentService` and destroyed safely without cross-thread lock inversion.
4. UI-thread `m_pages` and `m_document` are unreferenced without blocking the main loop.

### 4.5 Save Failure Error Handling
If `engine.saveProject()` or `saveAnnotations()` encounters an error:
1. The save status pill is updated to `● Save Failed` (red).
2. A modal `GtkMessageDialog` (`GTK_MESSAGE_ERROR`) is shown with the exact failure details.
3. The session remains marked dirty so the user can retry saving.

### 4.6 Document Tabs Scoping Note
In-pane Document Tabs (switching between multiple loaded PDFs in the left viewport pane) are explicitly scoped and deferred to a dedicated follow-up task (`TASK-5.3: Document Tabs & Multi-Doc Viewport Switcher`). In `TASK-5.2`, opening a PDF displays it in `DocumentPane` and registers it in `PdfDocumentService` for excerpt rendering across the canvas.
