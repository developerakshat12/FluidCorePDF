#include "workspace/TopToolbarWidget.h"

namespace FluidCoreApp {

namespace {

GtkWidget* createToolToggleBtn(const char* label, const char* tooltip) {
    GtkWidget* btn = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_set_can_focus(btn, FALSE);
    GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(ctx, "fc-tool-btn");
    return btn;
}

GtkWidget* createActionBtn(const char* label, const char* tooltip) {
    GtkWidget* btn = gtk_button_new_with_label(label);
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_set_can_focus(btn, FALSE);
    GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(ctx, "fc-tool-btn");
    return btn;
}

GtkWidget* createSeparator() {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    GtkStyleContext* ctx = gtk_widget_get_style_context(sep);
    gtk_style_context_add_class(ctx, "fc-toolbar-sep");
    return sep;
}

} // namespace

TopToolbarWidget::TopToolbarWidget(ToolManager& toolManager) : m_toolManager(toolManager) {
    setupStyles();
    createWidgets();

    m_toolManager.addChangeListener([this](Tool tool) { onToolStateChanged(tool); });
    onToolStateChanged(m_toolManager.activeTool());
}

TopToolbarWidget::~TopToolbarWidget() = default;

void TopToolbarWidget::setupStyles() {
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = ".fc-top-toolbar-strip {"
                      "  background: transparent;"
                      "  padding: 3px 0 3px 0;"
                      "}"
                      ".fc-top-toolbar-pill {"
                      "  background-color: rgba(255, 255, 255, 0.96);"
                      "  border: 1px solid #cbd5e1;"
                      "  border-radius: 20px;"
                      "  padding: 3px 8px;"
                      "  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.14);"
                      "}"
                      ".fc-tool-btn {"
                      "  min-height: 26px;"
                      "  min-width: 28px;"
                      "  padding: 2px 7px;"
                      "  margin: 0 1px;"
                      "  border-radius: 13px;"
                      "  border: 1px solid transparent;"
                      "  background: transparent;"
                      "  color: #334155;"
                      "  font-weight: 600;"
                      "  font-size: 12px;"
                      "}"
                      ".fc-tool-btn:hover {"
                      "  background-color: #f1f5f9;"
                      "  border-color: #cbd5e1;"
                      "  color: #0f172a;"
                      "}"
                      ".fc-tool-btn:checked, .fc-tool-btn:active {"
                      "  background-color: #2563eb;"
                      "  border-color: #1d4ed8;"
                      "  color: #ffffff;"
                      "}"
                      ".fc-tool-btn:disabled {"
                      "  color: #94a3b8;"
                      "  background: transparent;"
                      "}"
                      ".fc-toolbar-sep {"
                      "  margin: 3px 5px;"
                      "  min-width: 1px;"
                      "  background-color: #cbd5e1;"
                      "}";

    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void TopToolbarWidget::createWidgets() {
    m_rootContainer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(m_rootContainer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(m_rootContainer, GTK_ALIGN_START);

    GtkStyleContext* rootCtx = gtk_widget_get_style_context(m_rootContainer);
    gtk_style_context_add_class(rootCtx, "fc-top-toolbar-strip");

    m_pillBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkStyleContext* pillCtx = gtk_widget_get_style_context(m_pillBox);
    gtk_style_context_add_class(pillCtx, "fc-top-toolbar-pill");
    gtk_box_pack_start(GTK_BOX(m_rootContainer), m_pillBox, FALSE, FALSE, 0);

    // 1. Tool Toggle Buttons (Exclusive Radio-like behavior)
    m_selectBtn = createToolToggleBtn("↖ Select", "Pointer & Selection Tool [Esc / S]");
    m_penBtn = createToolToggleBtn("✏️ Pen", "Ink Pen Drawing Tool [P / Alt+1]");
    m_highlighterBtn = createToolToggleBtn("🖊️ Highlight", "Highlighter Tool [H / Alt+2]");
    m_eraserBtn = createToolToggleBtn("🧹 Eraser", "Stroke Eraser Tool [E / Alt+3]");
    m_cropBtn = createToolToggleBtn("✂️ Crop", "Excerpt Crop Tool [C / Alt+5]");
    m_connectorBtn = createToolToggleBtn("🔗 Link", "Ink Connector / Edge Tool [A / Alt+6]");

    gtk_box_pack_start(GTK_BOX(m_pillBox), m_selectBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_penBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_highlighterBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_eraserBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_cropBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_connectorBtn, FALSE, FALSE, 0);

    auto wireToolBtn = [this](GtkWidget* btn, Tool tool) {
        struct ToolBtnCtx {
            TopToolbarWidget* self;
            Tool tool;
        };
        auto* ctx = new ToolBtnCtx{this, tool};
        g_signal_connect_data(
            btn, "toggled", G_CALLBACK(+[](GtkToggleButton* tb, gpointer data) {
                auto* c = static_cast<ToolBtnCtx*>(data);
                if (c && !c->self->m_updatingToolUI && gtk_toggle_button_get_active(tb)) {
                    c->self->m_toolManager.setActiveTool(c->tool);
                }
            }),
            ctx, [](gpointer d, GClosure*) { delete static_cast<ToolBtnCtx*>(d); },
            static_cast<GConnectFlags>(0));
    };

    wireToolBtn(m_selectBtn, Tool::Select);
    wireToolBtn(m_penBtn, Tool::Pen);
    wireToolBtn(m_highlighterBtn, Tool::Highlighter);
    wireToolBtn(m_eraserBtn, Tool::Eraser);
    wireToolBtn(m_cropBtn, Tool::Crop);
    wireToolBtn(m_connectorBtn, Tool::Connector);

    // Separator
    gtk_box_pack_start(GTK_BOX(m_pillBox), createSeparator(), FALSE, FALSE, 0);

    // 2. History Buttons
    m_undoBtn = createActionBtn("↩ Undo", "Undo [Ctrl+Z]");
    m_redoBtn = createActionBtn("↪ Redo", "Redo [Ctrl+Shift+Z / Ctrl+Y]");
    gtk_widget_set_sensitive(m_undoBtn, FALSE);
    gtk_widget_set_sensitive(m_redoBtn, FALSE);

    g_signal_connect(m_undoBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onUndo)
                             self->m_onUndo();
                     }),
                     this);

    g_signal_connect(m_redoBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onRedo)
                             self->m_onRedo();
                     }),
                     this);

    gtk_box_pack_start(GTK_BOX(m_pillBox), m_undoBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_redoBtn, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(m_pillBox), createSeparator(), FALSE, FALSE, 0);

    // 3. Navigation & Canvas View
    m_zoomInBtn = createActionBtn("🔍+", "Zoom In [+]");
    m_zoomOutBtn = createActionBtn("🔍-", "Zoom Out [-]");
    m_resetViewBtn = createActionBtn("🎯 Reset", "Reset Canvas View [Ctrl+0]");
    m_minimapBtn = createToolToggleBtn("🗺️ Minimap", "Toggle Canvas Minimap [Ctrl+M]");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_minimapBtn), TRUE);

    g_signal_connect(m_zoomInBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onZoomIn)
                             self->m_onZoomIn();
                     }),
                     this);

    g_signal_connect(m_zoomOutBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onZoomOut)
                             self->m_onZoomOut();
                     }),
                     this);

    g_signal_connect(m_resetViewBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onResetView)
                             self->m_onResetView();
                     }),
                     this);

    g_signal_connect(m_minimapBtn, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onToggleMinimap)
                             self->m_onToggleMinimap();
                     }),
                     this);

    gtk_box_pack_start(GTK_BOX(m_pillBox), m_zoomInBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_zoomOutBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_resetViewBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_minimapBtn, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(m_pillBox), createSeparator(), FALSE, FALSE, 0);

    // 4. Global Action Buttons
    m_searchBtn = createActionBtn("🔍 Search", "Search Document & Canvas [Ctrl+F]");
    m_exportBtn = createActionBtn("📤 Export", "Export Synthesis to Markdown [Ctrl+E]");

    g_signal_connect(m_searchBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onSearch)
                             self->m_onSearch();
                     }),
                     this);

    g_signal_connect(m_exportBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_onExport)
                             self->m_onExport();
                     }),
                     this);

    gtk_box_pack_start(GTK_BOX(m_pillBox), m_searchBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_exportBtn, FALSE, FALSE, 0);
}

void TopToolbarWidget::onToolStateChanged(Tool tool) {
    m_updatingToolUI = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_selectBtn), tool == Tool::Select);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_penBtn), tool == Tool::Pen);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_highlighterBtn), tool == Tool::Highlighter);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_eraserBtn), tool == Tool::Eraser);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_cropBtn), tool == Tool::Crop);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_connectorBtn), tool == Tool::Connector);
    m_updatingToolUI = false;
}

void TopToolbarWidget::updateUndoRedoState(bool canUndo, bool canRedo) {
    if (m_undoBtn) {
        gtk_widget_set_sensitive(m_undoBtn, canUndo ? TRUE : FALSE);
    }
    if (m_redoBtn) {
        gtk_widget_set_sensitive(m_redoBtn, canRedo ? TRUE : FALSE);
    }
}

void TopToolbarWidget::setMinimapActive(bool active) {
    if (m_minimapBtn) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_minimapBtn), active ? TRUE : FALSE);
    }
}

} // namespace FluidCoreApp
