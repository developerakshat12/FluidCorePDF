#include "workspace/TopToolbarWidget.h"
#include "ui/AppIcons.h"

namespace FluidCoreApp {

namespace {

GtkWidget* createToolToggleBtn(AppIcon icon, const char* label, const char* tooltip,
                               GtkWidget** outIconWidget = nullptr) {
    GtkWidget* btn = gtk_toggle_button_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* iconWidget = AppIcons::createIconWidget(icon, AppIcons::ToolbarSize, IconState::Default);
    gtk_box_pack_start(GTK_BOX(box), iconWidget, FALSE, FALSE, 0);

    if (label && *label != '\0') {
        GtkWidget* labelWidget = gtk_label_new(label);
        gtk_box_pack_start(GTK_BOX(box), labelWidget, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(btn), box);

    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_set_can_focus(btn, FALSE);
    GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(ctx, "fc-tool-btn");

    if (outIconWidget) {
        *outIconWidget = iconWidget;
    }
    return btn;
}

GtkWidget* createActionBtn(AppIcon icon, const char* label, const char* tooltip,
                           const char* accessibleName = nullptr,
                           GtkWidget** outIconWidget = nullptr) {
    GtkWidget* btn = gtk_button_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* iconWidget = AppIcons::createIconWidget(icon, AppIcons::ToolbarSize, IconState::Default);
    gtk_box_pack_start(GTK_BOX(box), iconWidget, FALSE, FALSE, 0);

    if (label && *label != '\0') {
        GtkWidget* labelWidget = gtk_label_new(label);
        gtk_box_pack_start(GTK_BOX(box), labelWidget, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(btn), box);

    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_set_can_focus(btn, FALSE);
    GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(ctx, "fc-tool-btn");

    const char* accName = accessibleName ? accessibleName : (label && *label != '\0' ? label : tooltip);
    if (accName) {
        AtkObject* accessible = gtk_widget_get_accessible(btn);
        if (accessible) {
            atk_object_set_name(accessible, accName);
        }
    }

    if (outIconWidget) {
        *outIconWidget = iconWidget;
    }
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
    m_selectBtn = createToolToggleBtn(AppIcon::Select, "Select", "Pointer & Selection Tool [Esc / S]", &m_selectIcon);
    m_penBtn = createToolToggleBtn(AppIcon::Pen, "Pen", "Ink Pen Drawing Tool [P / Alt+1]", &m_penIcon);
    m_highlighterBtn = createToolToggleBtn(AppIcon::Highlighter, "Highlight", "Highlighter Tool [H / Alt+2]", &m_highlighterIcon);
    m_eraserBtn = createToolToggleBtn(AppIcon::Eraser, "Eraser", "Stroke Eraser Tool [E / Alt+3]", &m_eraserIcon);
    m_cropBtn = createToolToggleBtn(AppIcon::Crop, "Crop", "Excerpt Crop Tool [C / Alt+5]", &m_cropIcon);
    m_connectorBtn = createToolToggleBtn(AppIcon::Link, "Link", "Ink Connector / Edge Tool [A / Alt+6]", &m_connectorIcon);

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

    // 2. History Buttons (Compact icon-only with accessible names and shortcut tooltips)
    m_undoBtn = createActionBtn(AppIcon::Undo, "", "Undo [Ctrl+Z]", "Undo", &m_undoIcon);
    m_redoBtn = createActionBtn(AppIcon::Redo, "", "Redo [Ctrl+Shift+Z / Ctrl+Y]", "Redo", &m_redoIcon);
    gtk_widget_set_sensitive(m_undoBtn, FALSE);
    gtk_widget_set_sensitive(m_redoBtn, FALSE);
    AppIcons::setIconState(m_undoIcon, AppIcon::Undo, IconState::Disabled);
    AppIcons::setIconState(m_redoIcon, AppIcon::Redo, IconState::Disabled);

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
    m_zoomInBtn = createActionBtn(AppIcon::ZoomIn, "", "Zoom In [Ctrl++ / +]", "Zoom In");
    m_zoomOutBtn = createActionBtn(AppIcon::ZoomOut, "", "Zoom Out [Ctrl+- / -]", "Zoom Out");
    m_resetViewBtn = createActionBtn(AppIcon::ResetView, "Reset", "Reset Canvas View [Ctrl+0]", "Reset View");
    m_minimapBtn = createToolToggleBtn(AppIcon::Minimap, "Minimap", "Toggle Canvas Minimap [Ctrl+M]");
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
    m_searchBtn = createActionBtn(AppIcon::Search, "Search", "Search Document & Canvas [Ctrl+F]", "Search");
    m_exportBtn = createActionBtn(AppIcon::Export, "Export", "Export Synthesis to Markdown [Ctrl+E]", "Export");

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

    auto updateTool = [](GtkWidget* btn, GtkWidget* iconWidget, AppIcon icon, bool active) {
        if (btn && GTK_IS_TOGGLE_BUTTON(btn)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), active);
        }
        if (iconWidget) {
            AppIcons::setIconState(iconWidget, icon, active ? IconState::Active : IconState::Default);
        }
    };

    updateTool(m_selectBtn, m_selectIcon, AppIcon::Select, tool == Tool::Select);
    updateTool(m_penBtn, m_penIcon, AppIcon::Pen, tool == Tool::Pen);
    updateTool(m_highlighterBtn, m_highlighterIcon, AppIcon::Highlighter, tool == Tool::Highlighter);
    updateTool(m_eraserBtn, m_eraserIcon, AppIcon::Eraser, tool == Tool::Eraser);
    updateTool(m_cropBtn, m_cropIcon, AppIcon::Crop, tool == Tool::Crop);
    updateTool(m_connectorBtn, m_connectorIcon, AppIcon::Link, tool == Tool::Connector);

    m_updatingToolUI = false;
}

void TopToolbarWidget::updateUndoRedoState(bool canUndo, bool canRedo) {
    if (m_undoBtn && GTK_IS_WIDGET(m_undoBtn)) {
        gtk_widget_set_sensitive(m_undoBtn, canUndo ? TRUE : FALSE);
    }
    if (m_redoBtn && GTK_IS_WIDGET(m_redoBtn)) {
        gtk_widget_set_sensitive(m_redoBtn, canRedo ? TRUE : FALSE);
    }
    if (m_undoIcon) {
        AppIcons::setIconState(m_undoIcon, AppIcon::Undo, canUndo ? IconState::Default : IconState::Disabled);
    }
    if (m_redoIcon) {
        AppIcons::setIconState(m_redoIcon, AppIcon::Redo, canRedo ? IconState::Default : IconState::Disabled);
    }
}

void TopToolbarWidget::setMinimapActive(bool active) {
    if (m_minimapBtn && GTK_IS_TOGGLE_BUTTON(m_minimapBtn)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_minimapBtn), active ? TRUE : FALSE);
    }
}

} // namespace FluidCoreApp
