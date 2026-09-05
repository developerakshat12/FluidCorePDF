# Grounded Integration Plan: Fluid Synthesis Features for Xournal++ via libfluidcore

This document details the concrete integration points in the Xournal++ codebase (`src/core/`) to connect the **Decoupled C++ Core Engine (`libfluidcore`)** to Xournal++'s **GTK 3 / Cairo frontend**. These hooks enable **dual-viewport split rendering**, **cross-pane GTK3 drag-and-drop excerpt extraction**, **persistent bi-directional breadcrumb navigation with floating return pills**, and **multi-hit search context aggregation**.

---

## 1. Dual-Viewport Split-Screen Shell (`GtkPaned` Container in `MainWindow`)

* **Target Behavior Implemented**: Running the document pane and an independent spatial workspace pane side-by-side with a fluid, draggable splitter and full-screen toggle presets.
* **Exact Xournal++ Entry Points**:
  * `src/core/gui/MainWindow.h` / `MainWindow.cpp`: `MainWindow::initXournalWidget()`, `MainWindow::winXournal`, `MainWindow::xournal`.
  * `src/core/gui/Layout.h` / `Layout.cpp`: Layout adjustment and scroll synchronization.
  * `src/core/control/Control.h` / `Control.cpp`: Application controller managing window references.

### Step-by-Step GTK3 Wiring & Implementation
1. **Declare Split Container Members in `MainWindow.h`**:
   ```cpp
   GtkWidget* mainPanedSplitter = nullptr;
   GtkWidget* winWorkspace = nullptr;
   std::unique_ptr<ScrollHandling> workspaceScrollHandling;
   std::unique_ptr<WorkspaceView> workspaceView;
   ```
2. **Initialize Split View in `MainWindow::initXournalWidget()` (GTK 3.24+ Compliant)**:
   ```cpp
   void MainWindow::initXournalWidget() {
       // Create resizable horizontal split container (GTK3)
       this->mainPanedSplitter = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
       gtk_container_add(GTK_CONTAINER(get("boxContents")), this->mainPanedSplitter);

       // Left Pane: Document Viewport
       this->winXournal = gtk_scrolled_window_new(nullptr, nullptr);
       gtk_widget_set_hexpand(this->winXournal, TRUE);
       gtk_widget_set_vexpand(this->winXournal, TRUE);
       gtk_paned_pack1(GTK_PANED(this->mainPanedSplitter), this->winXournal, TRUE, FALSE);

       this->scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLED_WINDOW(this->winXournal));
       this->xournal = std::make_unique<XournalView>(this->winXournal, this->control, this->scrollHandling.get());

       // Right Pane: Workspace Canvas (no GtkScrolledWindow wrapper - WorkspaceView owns its
       // own affine viewport matrix and handles pan/zoom/scroll internally)
       this->workspaceView = std::make_unique<WorkspaceView>(this->control, this->control->getFluidCoreAPI());
       this->winWorkspace = this->workspaceView->getWidget();
       gtk_widget_set_hexpand(this->winWorkspace, TRUE);
       gtk_widget_set_vexpand(this->winWorkspace, TRUE);
       gtk_paned_pack2(GTK_PANED(this->mainPanedSplitter), this->winWorkspace, TRUE, FALSE);

       // Initial split position: proportional (half of available width) when the window size
       // is known; fixed 600px fallback when called before layout allocation (not proportional).
       gint contentsWidth = gtk_widget_get_allocated_width(get("boxContents"));
       gtk_paned_set_position(GTK_PANED(this->mainPanedSplitter),
                              contentsWidth > 0 ? contentsWidth / 2 : 600);
       gtk_widget_show_all(this->mainPanedSplitter);
   }
   ```
3. **Add Splitter Position Hotkeys and Actions**:
   * In `src/core/control/Control.cpp`, add actions `ACTION_SPLIT_50_50`, `ACTION_SPLIT_DOC_FULL`, and `ACTION_SPLIT_WORKSPACE_FULL` to adjust `gtk_paned_set_position()`.

---

## 2. Cross-Pane Spatial Drag-and-Drop Excerpt Initiator (GTK3 DND)

* **Target Behavior Implemented**: Selecting text or lassoing a region in the PDF triggers a draggable ghost that can be dragged across the pane boundary and dropped at an arbitrary $(X, Y)$ coordinate onto the parallel canvas.
* **Exact Xournal++ Entry Points**:
  * `src/core/gui/PdfFloatingToolbox.h` / `PdfFloatingToolbox.cpp`: `PdfFloatingToolbox::PdfFloatingToolbox`, `newSelection()`, `getSelection()`.
  * `src/core/control/tools/PdfElemSelection.h` / `PdfElemSelection.cpp`: `PdfElemSelection::getSelectedText()`, `PdfElemSelection::getSelectedTextRects()`.
  * `src/core/gui/workspace/WorkspaceView.cpp`: Drag destination widget.

### Step-by-Step Wiring & Implementation
1. **Package Excerpt Payload Structure**:
   ```cpp
   struct ExcerptDropPayload {
       std::string docUuid;
       std::string textContent;
       size_t sourcePageNumber;
       XojPdfRectangle sourceNormalizedRect;
       bool rasterPending = true;
       // Note: cairo_surface_t* is intentionally never transferred through the GTK3 selection
       // buffer (Wayland selection limits / memory leaks). Instead, rasterPending signals the
       // destination view to create the card in a pending/loading state and fetch the raster
       // asynchronously from PdfCache keyed by (docUuid, sourcePageNumber, sourceNormalizedRect).
   };
   ```
2. **Configure Drag Gesture Controller & GTK3 Drag Source**:
   ```cpp
   static const GtkTargetEntry targetEntries[] = {
       {(gchar*)"application/x-fluid-excerpt", GTK_TARGET_SAME_APP, 0}
   };

   void PdfFloatingToolbox::initDragSource() {
       GtkWidget* dragBtn = theMainWindow->get("pdfTbDragHandle");
       gtk_drag_source_set(dragBtn, GDK_BUTTON1_MASK, targetEntries, 1, GDK_ACTION_COPY);
       
       g_signal_connect(dragBtn, "drag-data-get", G_CALLBACK(+[](GtkWidget* widget, GdkDragContext* context, 
                                                                  GtkSelectionData* selectionData, guint info, 
                                                                  guint time, gpointer data) {
           auto* toolbox = static_cast<PdfFloatingToolbox*>(data);
           auto* sel = toolbox->getSelection();
           if (!sel) return;

           std::string serialized = serializeExcerptPayload(sel);
           gtk_selection_data_set(selectionData, gdk_atom_intern("application/x-fluid-excerpt", FALSE),
                                  8, (const guchar*)serialized.data(), serialized.size());
       }), this);
   }
   ```
3. **Configure GTK3 Drag Destination on `WorkspaceView`**:
   ```cpp
   void WorkspaceView::initDragDest() {
       gtk_drag_dest_set(this->widget, GTK_DEST_DEFAULT_ALL, targetEntries, 1, GDK_ACTION_COPY);
       g_signal_connect(this->widget, "drag-data-received", G_CALLBACK(+[](GtkWidget* widget, GdkDragContext* context,
                                                                           gint x, gint y, GtkSelectionData* data,
                                                                           guint info, guint time, gpointer user_data) {
           auto* wsView = static_cast<WorkspaceView*>(user_data);
           const guchar* rawData = gtk_selection_data_get_data(data);
           gint length = gtk_selection_data_get_length(data);
           
           if (length > 0) {
               ExcerptDropPayload payload = deserializeExcerptPayload(rawData, length);
               Point worldCoord = wsView->screenToWorld(Point(x, y));

               // Create the card immediately in a pending/loading state; the raster tile is
               // fetched asynchronously from PdfCache and populated via callback so the GTK
               // main thread is never blocked.
               ExcerptCardNode* card = wsView->createExcerptCard(payload, worldCoord);
               wsView->fetchRasterAsync(card, payload.docUuid, payload.sourcePageNumber,
                                        payload.sourceNormalizedRect);

               gtk_drag_finish(context, TRUE, FALSE, time);
           } else {
               gtk_drag_finish(context, FALSE, FALSE, time);
           }
       }), this);
   }
   ```

---

## 3. Persistent Bi-Directional Link Navigation with Floating Return Pills

* **Target Behavior Implemented**: Tapping an excerpt's source anchor scrolls the document smoothly to the target page with a luminous highlight pulse, and renders an interactive, floating "Return to Excerpt" pill in the document viewport overlay pass so the user can easily jump back at any time.
* **Exact Xournal++ Entry Points**:
  * `src/core/control/NavigationHistory.h` / `NavigationHistory.cpp`: `NavigationHistory::recordNavPoint()`.
  * `src/core/control/ScrollHandler.h` / `ScrollHandler.cpp`: `ScrollHandler::scrollToPdfPage(size_t page, double left, double top)`.
  * `src/core/gui/widgets/ReturnAnchorPill.h` / `.cpp`: Viewport overlay component rendered directly in the `XournalView` Cairo paint pass, receiving clicks via hit-testing in `XournalView::button-press-event`.

### Step-by-Step Wiring & Implementation
1. **Create Persistent Floating Return Pill (`ReturnAnchorPill`)**:
   * Implement `src/core/gui/widgets/ReturnAnchorPill.h` / `.cpp` as a viewport overlay element tracking document coordinates and participating in hit testing.
   * Stores return state: `Point workspaceTargetCoord`, `std::string excerptTitle`, `std::string sourceCardId`.
2. **Execute Smooth Jump with Luminous Pulse & Pill Placement**:
   ```cpp
   void NavigationHistory::navigateToSourceWithReturnAnchor(size_t page, const XojPdfRectangle& rect, 
                                                            Point originWorkspacePt, const std::string& cardId) {
       // 1. Snapshot current position
       this->recordNavPoint();

       // 2. Smoothly scroll document pane to exact bounding box
        control->getScrollHandler()->scrollToPdfPage(page, rect.x0, rect.y0);

       // 3. Trigger pulse highlight animation over source bounding box
       XojPageView* pageView = control->getWindow()->getXournal()->getPageViewForPdfPage(page);
       if (pageView) {
           pageView->triggerPulseHighlight(rect);
       }

       // 4. Attach floating return pill to document margin overlay
       control->getWindow()->showReturnPill("Back to Excerpt #" + cardId.substr(0, 6), originWorkspacePt);
   }
   ```
3. **Handle Return Pill Click**:
   * When clicked, invokes `control->getWorkspaceView()->scrollToWorldCoordinate(originWorkspacePt.x, originWorkspacePt.y)` and smoothly dismisses the pill.

---

## 4. Multi-Hit Search Context Aggregator (Sequential Search Slice View)

* **Target Behavior Implemented**: A consolidated search overview that extracts and displays all search hits sequentially with surrounding context sentences in a continuous scrollable reading stream.
* **Exact Xournal++ Entry Points**:
  * `src/core/gui/SearchBar.h` / `SearchBar.cpp`: `SearchBar::searchTextChangedCallback()`.
  * `src/core/pdf/base/XojPdfPage.h`: `XojPdfPage::findText(const std::string& text)`, `XojPdfPage::getSurroundingTextBlock(const XojPdfRectangle& hitRect)`.
  * `src/core/gui/sidebar/AbstractSidebarPage.h`: Sidebar tab integration.

### Step-by-Step Wiring & Implementation
1. **Create Search Context Stream Sidebar Component (`SidebarSearchSlicesPage`)**:
   * Create `src/core/gui/sidebar/search/SidebarSearchSlicesPage.h` / `.cpp` inheriting from `AbstractSidebarPage`.
   * UI consists of a `GtkScrolledWindow` containing a vertical `GtkBox` of context slice cards.
2. **Extract Semantic Context Sentences Around Search Hits**:
   ```cpp
   void SidebarSearchSlicesPage::performSearch(const std::string& query) {
       this->clearSlices();
       auto* doc = control->getDocument();
       size_t totalPages = doc->getPageCount();

       for (size_t p = 0; p < totalPages; ++p) {
           auto pdfPage = doc->getPdfPage(p);
           if (!pdfPage) continue;
           
           auto hits = pdfPage->findText(query);
           for (const auto& hit : hits) {
               // Extract paragraph block boundary using Poppler text block layout analysis
               std::string contextText = pdfPage->getSurroundingTextBlock(hit);

               // Insert Context Card into UI stream
               this->addSearchHitSlice(p, hit, contextText);
           }
       }
   }
   ```
3. **Wire Instant Page Jump on Slice Activation**:
   * Clicking any search slice card smoothly scrolls the document pane directly to `(p, hit.x0, hit.y0)` with highlight focus.

---

*This concludes the grounded integration plan for Xournal++ via libfluidcore.*
