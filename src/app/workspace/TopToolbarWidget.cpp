#include "workspace/TopToolbarWidget.h"
#include "ui/AppIcons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace FluidCoreApp {

namespace {

GtkWidget* createToolToggleBtn(AppIcon icon, const char* label, const char* tooltip,
                               GtkWidget** outIconWidget = nullptr) {
    GtkWidget* btn = gtk_toggle_button_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* iconWidget =
        AppIcons::createIconWidget(icon, AppIcons::ToolbarSize, IconState::Default);
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
    GtkWidget* iconWidget =
        AppIcons::createIconWidget(icon, AppIcons::ToolbarSize, IconState::Default);
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

    const char* accName =
        accessibleName ? accessibleName : (label && *label != '\0' ? label : tooltip);
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
    m_toolManager.addPropertyChangeListener(
        [this](Tool, const InkProperties&) { updateInkingUI(); });
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
                      ".fc-color-pill, .fc-weight-pill {"
                      "  min-height: 26px;"
                      "  padding: 2px 8px;"
                      "  margin: 0 1px;"
                      "  border-radius: 13px;"
                      "  border: 1px solid #e2e8f0;"
                      "  background-color: #f8fafc;"
                      "  color: #334155;"
                      "  font-weight: 600;"
                      "  font-size: 11px;"
                      "}"
                      ".fc-color-pill:hover, .fc-weight-pill:hover {"
                      "  background-color: #f1f5f9;"
                      "  border-color: #cbd5e1;"
                      "  color: #0f172a;"
                      "}"
                      ".fc-color-pill:disabled, .fc-weight-pill:disabled {"
                      "  opacity: 0.35;"
                      "  background: transparent;"
                      "  border-color: transparent;"
                      "  color: #94a3b8;"
                      "}"
                      ".fc-arrow-label {"
                      "  font-size: 8px;"
                      "  color: #64748b;"
                      "  margin-left: 2px;"
                      "}"
                      ".fc-popover-box {"
                      "  padding: 10px;"
                      "  background-color: #ffffff;"
                      "}"
                      ".fc-popover-title {"
                      "  font-size: 11px;"
                      "  font-weight: 700;"
                      "  color: #64748b;"
                      "  margin-bottom: 4px;"
                      "}"
                      ".fc-swatch-btn {"
                      "  min-width: 26px;"
                      "  min-height: 26px;"
                      "  border-radius: 13px;"
                      "  border: 1px solid #e2e8f0;"
                      "  padding: 0;"
                      "  margin: 1px;"
                      "  background: transparent;"
                      "}"
                      ".fc-swatch-btn:hover {"
                      "  background-color: #f1f5f9;"
                      "  border-color: #94a3b8;"
                      "}"
                      ".fc-preset-btn {"
                      "  min-height: 24px;"
                      "  min-width: 38px;"
                      "  padding: 2px 6px;"
                      "  border-radius: 12px;"
                      "  border: 1px solid #e2e8f0;"
                      "  background-color: #f8fafc;"
                      "  font-size: 11px;"
                      "  font-weight: 600;"
                      "  color: #334155;"
                      "  margin: 0 2px;"
                      "}"
                      ".fc-preset-btn:hover {"
                      "  background-color: #e2e8f0;"
                      "  color: #0f172a;"
                      "}"
                      ".fc-preset-btn.active-preset {"
                      "  background-color: #2563eb;"
                      "  color: #ffffff;"
                      "  border-color: #1d4ed8;"
                      "}"
                      ".fc-stepper-btn {"
                      "  min-width: 24px;"
                      "  min-height: 24px;"
                      "  border-radius: 12px;"
                      "  border: 1px solid #e2e8f0;"
                      "  background: #f8fafc;"
                      "  font-weight: bold;"
                      "  font-size: 12px;"
                      "  padding: 0;"
                      "}"
                      ".fc-preview-frame {"
                      "  background: #f8fafc;"
                      "  border: 1px solid #e2e8f0;"
                      "  border-radius: 8px;"
                      "  margin-bottom: 6px;"
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
    m_selectBtn = createToolToggleBtn(AppIcon::Select, "Select",
                                      "Pointer & Selection Tool [Esc / S]", &m_selectIcon);
    m_penBtn =
        createToolToggleBtn(AppIcon::Pen, "Pen", "Ink Pen Drawing Tool [P / Alt+1]", &m_penIcon);
    m_highlighterBtn = createToolToggleBtn(AppIcon::Highlighter, "Highlight",
                                           "Highlighter Tool [H / Alt+2]", &m_highlighterIcon);
    m_eraserBtn = createToolToggleBtn(AppIcon::Eraser, "Eraser", "Stroke Eraser Tool [E / Alt+3]",
                                      &m_eraserIcon);
    m_cropBtn =
        createToolToggleBtn(AppIcon::Crop, "Crop", "Excerpt Crop Tool [C / Alt+5]", &m_cropIcon);
    m_connectorBtn = createToolToggleBtn(AppIcon::Link, "Link",
                                         "Ink Connector / Edge Tool [A / Alt+6]", &m_connectorIcon);

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

    // Separator and Inking Properties Pill Controls
    m_inkingSep = createSeparator();
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_inkingSep, FALSE, FALSE, 0);
    createInkingControls();

    // Separator
    gtk_box_pack_start(GTK_BOX(m_pillBox), createSeparator(), FALSE, FALSE, 0);

    // 2. History Buttons (Compact icon-only with accessible names and shortcut tooltips)
    m_undoBtn = createActionBtn(AppIcon::Undo, "", "Undo [Ctrl+Z]", "Undo", &m_undoIcon);
    m_redoBtn =
        createActionBtn(AppIcon::Redo, "", "Redo [Ctrl+Shift+Z / Ctrl+Y]", "Redo", &m_redoIcon);
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
    m_resetViewBtn =
        createActionBtn(AppIcon::ResetView, "Reset", "Reset Canvas View [Ctrl+0]", "Reset View");
    m_minimapBtn =
        createToolToggleBtn(AppIcon::Minimap, "Minimap", "Toggle Canvas Minimap [Ctrl+M]");
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
                         if (self && !self->m_updatingMinimapState && self->m_onToggleMinimap)
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
    m_searchBtn =
        createActionBtn(AppIcon::Search, "Search", "Search Document & Canvas [Ctrl+F]", "Search");
    m_exportBtn = createActionBtn(AppIcon::Export, "Export",
                                  "Export Synthesis to Markdown [Ctrl+E]", "Export");

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
            AppIcons::setIconState(iconWidget, icon,
                                   active ? IconState::Active : IconState::Default);
        }
    };

    updateTool(m_selectBtn, m_selectIcon, AppIcon::Select, tool == Tool::Select);
    updateTool(m_penBtn, m_penIcon, AppIcon::Pen, tool == Tool::Pen);
    updateTool(m_highlighterBtn, m_highlighterIcon, AppIcon::Highlighter,
               tool == Tool::Highlighter);
    updateTool(m_eraserBtn, m_eraserIcon, AppIcon::Eraser, tool == Tool::Eraser);
    updateTool(m_cropBtn, m_cropIcon, AppIcon::Crop, tool == Tool::Crop);
    updateTool(m_connectorBtn, m_connectorIcon, AppIcon::Link, tool == Tool::Connector);

    m_updatingToolUI = false;

    updateInkingUI();
}

void TopToolbarWidget::createInkingControls() {
    // 1. Color Button (Pill)
    m_colorBtn = gtk_button_new();
    GtkWidget* colorBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    m_colorSwatch = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_colorSwatch, 14, 14);
    g_signal_connect(m_colorSwatch, "draw",
                     G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer data) -> gboolean {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (!self)
                             return FALSE;
                         uint32_t c = self->m_toolManager.activeColor();
                         double r = ((c >> 16) & 0xFF) / 255.0;
                         double g = ((c >> 8) & 0xFF) / 255.0;
                         double b = (c & 0xFF) / 255.0;
                         cairo_arc(cr, 7.0, 7.0, 5.5, 0.0, 2.0 * M_PI);
                         cairo_set_source_rgb(cr, r, g, b);
                         cairo_fill_preserve(cr);
                         cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
                         cairo_set_line_width(cr, 1.0);
                         cairo_stroke(cr);
                         return FALSE;
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(colorBox), m_colorSwatch, FALSE, FALSE, 0);

    GtkWidget* colorArrow = gtk_label_new("▾");
    GtkStyleContext* arrCtx = gtk_widget_get_style_context(colorArrow);
    gtk_style_context_add_class(arrCtx, "fc-arrow-label");
    gtk_box_pack_start(GTK_BOX(colorBox), colorArrow, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(m_colorBtn), colorBox);
    gtk_widget_set_tooltip_text(m_colorBtn, "Inking Color & Palette");
    gtk_widget_set_can_focus(m_colorBtn, FALSE);
    GtkStyleContext* cCtx = gtk_widget_get_style_context(m_colorBtn);
    gtk_style_context_add_class(cCtx, "fc-color-pill");
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_colorBtn, FALSE, FALSE, 0);

    m_colorPopover = gtk_popover_new(m_colorBtn);
    gtk_popover_set_position(GTK_POPOVER(m_colorPopover), GTK_POS_BOTTOM);

    g_signal_connect(m_colorBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self) {
                             self->rebuildColorPopover();
                             gtk_popover_popup(GTK_POPOVER(self->m_colorPopover));
                         }
                     }),
                     this);

    // 2. Weight Button (Pill)
    m_weightBtn = gtk_button_new();
    GtkWidget* weightBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    m_weightDot = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_weightDot, 14, 14);
    g_signal_connect(
        m_weightDot, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer data) -> gboolean {
            auto* self = static_cast<TopToolbarWidget*>(data);
            if (!self)
                return FALSE;
            double w = self->m_toolManager.activeWidth();
            uint32_t c = self->m_toolManager.activeColor();
            double radius = std::clamp(w / 3.0, 1.5, 6.0);
            double r = ((c >> 16) & 0xFF) / 255.0;
            double g = ((c >> 8) & 0xFF) / 255.0;
            double b = (c & 0xFF) / 255.0;
            double alpha = (self->m_toolManager.activeTool() == Tool::Highlighter) ? 0.6 : 1.0;
            cairo_arc(cr, 7.0, 7.0, radius, 0.0, 2.0 * M_PI);
            cairo_set_source_rgba(cr, r, g, b, alpha);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.2);
            cairo_set_line_width(cr, 0.75);
            cairo_stroke(cr);
            return FALSE;
        }),
        this);
    gtk_box_pack_start(GTK_BOX(weightBox), m_weightDot, FALSE, FALSE, 0);

    m_weightLabel = gtk_label_new("2.0 px");
    gtk_box_pack_start(GTK_BOX(weightBox), m_weightLabel, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(m_weightBtn), weightBox);
    gtk_widget_set_tooltip_text(m_weightBtn, "Stroke Thickness & Presets [ [ / ] ]");
    gtk_widget_set_can_focus(m_weightBtn, FALSE);
    GtkStyleContext* wCtx = gtk_widget_get_style_context(m_weightBtn);
    gtk_style_context_add_class(wCtx, "fc-weight-pill");
    gtk_box_pack_start(GTK_BOX(m_pillBox), m_weightBtn, FALSE, FALSE, 0);

    m_weightPopover = gtk_popover_new(m_weightBtn);
    gtk_popover_set_position(GTK_POPOVER(m_weightPopover), GTK_POS_BOTTOM);

    g_signal_connect(m_weightBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self) {
                             self->rebuildWeightPopover();
                             gtk_popover_popup(GTK_POPOVER(self->m_weightPopover));
                         }
                     }),
                     this);

    updateInkingUI();
}

void TopToolbarWidget::updateInkingUI() {
    Tool currentTool = m_toolManager.activeTool();
    bool isInking = ToolManager::isInkingTool(currentTool);

    if (m_colorBtn && GTK_IS_WIDGET(m_colorBtn)) {
        gtk_widget_set_sensitive(m_colorBtn, isInking ? TRUE : FALSE);
    }
    if (m_weightBtn && GTK_IS_WIDGET(m_weightBtn)) {
        gtk_widget_set_sensitive(m_weightBtn, isInking ? TRUE : FALSE);
    }

    if (m_colorSwatch && GTK_IS_WIDGET(m_colorSwatch)) {
        gtk_widget_queue_draw(m_colorSwatch);
    }
    if (m_weightDot && GTK_IS_WIDGET(m_weightDot)) {
        gtk_widget_queue_draw(m_weightDot);
    }

    if (m_weightLabel && GTK_IS_LABEL(m_weightLabel)) {
        char buf[32];
        double w = m_toolManager.activeWidth();
        if (std::abs(w - std::round(w)) < 0.05) {
            std::snprintf(buf, sizeof(buf), "%d px", static_cast<int>(std::round(w)));
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f px", w);
        }
        gtk_label_set_text(GTK_LABEL(m_weightLabel), buf);
    }

    if (m_weightPreviewArea && GTK_IS_WIDGET(m_weightPreviewArea) &&
        gtk_widget_get_visible(m_weightPreviewArea)) {
        gtk_widget_queue_draw(m_weightPreviewArea);
    }

    if (m_weightScale && GTK_IS_RANGE(m_weightScale) && !m_updatingWeightScale) {
        m_updatingWeightScale = true;
        gtk_range_set_value(GTK_RANGE(m_weightScale), m_toolManager.activeWidth());
        m_updatingWeightScale = false;
    }
}

void TopToolbarWidget::rebuildColorPopover() {
    if (!m_colorPopover)
        return;
    GtkWidget* existing = gtk_bin_get_child(GTK_BIN(m_colorPopover));
    if (existing) {
        gtk_widget_destroy(existing);
    }

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkStyleContext* vCtx = gtk_widget_get_style_context(vbox);
    gtk_style_context_add_class(vCtx, "fc-popover-box");

    Tool activeTool = m_toolManager.activeTool();
    const char* titleText = (activeTool == Tool::Highlighter) ? "Highlighter Color" : "Pen Color";
    GtkWidget* title = gtk_label_new(titleText);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkStyleContext* tCtx = gtk_widget_get_style_context(title);
    gtk_style_context_add_class(tCtx, "fc-popover-title");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    // Curated Swatches
    std::vector<uint32_t> colors;
    if (activeTool == Tool::Highlighter) {
        colors = {0xFACC15, 0x22C55E, 0x0EA5E9, 0xEC4899, 0xF97316, 0xA855F7};
    } else {
        colors = {0x0F172A, 0x2563EB, 0xEF4444, 0x10B981, 0x8B5CF6, 0xF59E0B, 0x64748B, 0xEC4899};
    }

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);

    int col = 0;
    int row = 0;
    const int maxCols = (activeTool == Tool::Highlighter) ? 3 : 4;

    uint32_t curColor = m_toolManager.activeColor();

    for (uint32_t c : colors) {
        GtkWidget* btn = gtk_button_new();
        GtkWidget* swatch = gtk_drawing_area_new();
        gtk_widget_set_size_request(swatch, 24, 24);

        struct SwatchData {
            uint32_t color;
            bool selected;
        };
        auto* sData = new SwatchData{c, (c == curColor)};
        g_signal_connect_data(
            swatch, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer data) -> gboolean {
                auto* d = static_cast<SwatchData*>(data);
                if (!d)
                    return FALSE;
                double r = ((d->color >> 16) & 0xFF) / 255.0;
                double g = ((d->color >> 8) & 0xFF) / 255.0;
                double b = (d->color & 0xFF) / 255.0;

                cairo_arc(cr, 12.0, 12.0, 10.0, 0.0, 2.0 * M_PI);
                cairo_set_source_rgb(cr, r, g, b);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);

                if (d->selected) {
                    cairo_arc(cr, 12.0, 12.0, 5.0, 0.0, 2.0 * M_PI);
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
                    cairo_set_line_width(cr, 2.0);
                    cairo_stroke(cr);
                }
                return FALSE;
            }),
            sData, [](gpointer p, GClosure*) { delete static_cast<SwatchData*>(p); },
            static_cast<GConnectFlags>(0));

        gtk_container_add(GTK_CONTAINER(btn), swatch);
        GtkStyleContext* bCtx = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(bCtx, "fc-swatch-btn");

        struct ClickData {
            TopToolbarWidget* self;
            uint32_t color;
        };
        auto* cData = new ClickData{this, c};
        g_signal_connect_data(
            btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                auto* cd = static_cast<ClickData*>(data);
                if (cd && cd->self) {
                    cd->self->m_toolManager.setActiveColor(cd->color);
                    gtk_popover_popdown(GTK_POPOVER(cd->self->m_colorPopover));
                }
            }),
            cData, [](gpointer p, GClosure*) { delete static_cast<ClickData*>(p); },
            static_cast<GConnectFlags>(0));

        gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
        col++;
        if (col >= maxCols) {
            col = 0;
            row++;
        }
    }
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE,
                       4);

    // Hex Entry Row
    GtkWidget* hexRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* hexHash = gtk_label_new("#");
    gtk_box_pack_start(GTK_BOX(hexRow), hexHash, FALSE, FALSE, 0);

    m_colorHexEntry = gtk_entry_new();
    char hexBuf[16];
    std::snprintf(hexBuf, sizeof(hexBuf), "%06X", curColor);
    gtk_entry_set_text(GTK_ENTRY(m_colorHexEntry), hexBuf);
    gtk_entry_set_max_length(GTK_ENTRY(m_colorHexEntry), 6);
    gtk_entry_set_width_chars(GTK_ENTRY(m_colorHexEntry), 7);
    gtk_box_pack_start(GTK_BOX(hexRow), m_colorHexEntry, TRUE, TRUE, 0);

    GtkWidget* applyBtn = gtk_button_new_with_label("Apply");
    g_signal_connect(applyBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_colorHexEntry) {
                             const char* txt = gtk_entry_get_text(GTK_ENTRY(self->m_colorHexEntry));
                             if (txt && *txt != '\0') {
                                 if (*txt == '#')
                                     txt++;
                                 char* end = nullptr;
                                 unsigned long val = std::strtoul(txt, &end, 16);
                                 if (end != txt) {
                                     self->m_toolManager.setActiveColor(
                                         static_cast<uint32_t>(val & 0xFFFFFF));
                                     gtk_popover_popdown(GTK_POPOVER(self->m_colorPopover));
                                 }
                             }
                         }
                     }),
                     this);

    g_signal_connect(m_colorHexEntry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && self->m_colorHexEntry) {
                             const char* txt = gtk_entry_get_text(GTK_ENTRY(self->m_colorHexEntry));
                             if (txt && *txt != '\0') {
                                 if (*txt == '#')
                                     txt++;
                                 char* end = nullptr;
                                 unsigned long val = std::strtoul(txt, &end, 16);
                                 if (end != txt) {
                                     self->m_toolManager.setActiveColor(
                                         static_cast<uint32_t>(val & 0xFFFFFF));
                                     gtk_popover_popdown(GTK_POPOVER(self->m_colorPopover));
                                 }
                             }
                         }
                     }),
                     this);

    gtk_box_pack_start(GTK_BOX(hexRow), applyBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hexRow, FALSE, FALSE, 0);

    // Custom Color Button
    GtkWidget* customBtn = gtk_button_new_with_label("Custom Color...");
    g_signal_connect(
        customBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<TopToolbarWidget*>(data);
            if (!self)
                return;
            gtk_popover_popdown(GTK_POPOVER(self->m_colorPopover));

            GtkWidget* toplevel = gtk_widget_get_toplevel(self->m_rootContainer);
            GtkWidget* dialog = gtk_color_chooser_dialog_new(
                "Select Color", GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr);
            uint32_t cur = self->m_toolManager.activeColor();
            GdkRGBA rgba;
            rgba.red = ((cur >> 16) & 0xFF) / 255.0;
            rgba.green = ((cur >> 8) & 0xFF) / 255.0;
            rgba.blue = (cur & 0xFF) / 255.0;
            rgba.alpha = 1.0;
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &rgba);
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &rgba);
                uint32_t r = static_cast<uint32_t>(std::clamp(rgba.red * 255.0 + 0.5, 0.0, 255.0));
                uint32_t g =
                    static_cast<uint32_t>(std::clamp(rgba.green * 255.0 + 0.5, 0.0, 255.0));
                uint32_t b = static_cast<uint32_t>(std::clamp(rgba.blue * 255.0 + 0.5, 0.0, 255.0));
                self->m_toolManager.setActiveColor((r << 16) | (g << 8) | b);
            }
            gtk_widget_destroy(dialog);
        }),
        this);

    gtk_box_pack_start(GTK_BOX(vbox), customBtn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(m_colorPopover), vbox);
    gtk_widget_show_all(vbox);
}

void TopToolbarWidget::rebuildWeightPopover() {
    if (!m_weightPopover)
        return;
    GtkWidget* existing = gtk_bin_get_child(GTK_BIN(m_weightPopover));
    if (existing) {
        gtk_widget_destroy(existing);
    }

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkStyleContext* vCtx = gtk_widget_get_style_context(vbox);
    gtk_style_context_add_class(vCtx, "fc-popover-box");

    Tool activeTool = m_toolManager.activeTool();
    const char* titleText =
        (activeTool == Tool::Highlighter) ? "Highlighter Thickness" : "Pen Thickness";
    GtkWidget* title = gtk_label_new(titleText);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkStyleContext* tCtx = gtk_widget_get_style_context(title);
    gtk_style_context_add_class(tCtx, "fc-popover-title");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    // 1. Live Stroke Preview Canvas
    m_weightPreviewArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(m_weightPreviewArea, 220, 44);
    GtkStyleContext* pCtx = gtk_widget_get_style_context(m_weightPreviewArea);
    gtk_style_context_add_class(pCtx, "fc-preview-frame");

    g_signal_connect(
        m_weightPreviewArea, "draw",
        G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer data) -> gboolean {
            auto* self = static_cast<TopToolbarWidget*>(data);
            if (!self)
                return FALSE;

            GtkAllocation alloc;
            gtk_widget_get_allocation(w, &alloc);

            // Neutral light background
            cairo_set_source_rgb(cr, 0.97, 0.98, 0.99);
            cairo_paint(cr);

            // Draw preview stroke
            cairo_save(cr);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

            double strokeW = self->m_toolManager.activeWidth();
            cairo_set_line_width(cr, std::max(0.5, strokeW));

            uint32_t c = self->m_toolManager.activeColor();
            double r = ((c >> 16) & 0xFF) / 255.0;
            double g = ((c >> 8) & 0xFF) / 255.0;
            double b = (c & 0xFF) / 255.0;
            double a = (self->m_toolManager.activeTool() == Tool::Highlighter) ? 0.45 : 1.0;
            cairo_set_source_rgba(cr, r, g, b, a);

            cairo_move_to(cr, 20.0, alloc.height / 2.0 + 4.0);
            cairo_curve_to(cr, alloc.width * 0.35, alloc.height * 0.15, alloc.width * 0.65,
                           alloc.height * 0.85, alloc.width - 20.0, alloc.height / 2.0 - 4.0);
            cairo_stroke(cr);
            cairo_restore(cr);

            return FALSE;
        }),
        this);

    gtk_box_pack_start(GTK_BOX(vbox), m_weightPreviewArea, FALSE, FALSE, 0);

    // 2. Preset Buttons Row
    GtkWidget* presetsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    std::vector<double> presets;
    if (activeTool == Tool::Highlighter) {
        presets = {8.0, 14.0, 22.0, 32.0};
    } else {
        presets = {1.0, 2.0, 4.0, 8.0};
    }

    double currentW = m_toolManager.activeWidth();

    for (double p : presets) {
        char pBuf[16];
        if (p == static_cast<int>(p)) {
            std::snprintf(pBuf, sizeof(pBuf), "%d px", static_cast<int>(p));
        } else {
            std::snprintf(pBuf, sizeof(pBuf), "%.1f px", p);
        }

        GtkWidget* pBtn = gtk_button_new_with_label(pBuf);
        GtkStyleContext* bCtx = gtk_widget_get_style_context(pBtn);
        gtk_style_context_add_class(bCtx, "fc-preset-btn");
        if (std::abs(currentW - p) < 0.1) {
            gtk_style_context_add_class(bCtx, "active-preset");
        }

        struct PresetData {
            TopToolbarWidget* self;
            double width;
        };
        auto* pd = new PresetData{this, p};
        g_signal_connect_data(
            pBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                auto* d = static_cast<PresetData*>(data);
                if (d && d->self) {
                    d->self->m_toolManager.setActiveWidth(d->width);
                    d->self->rebuildWeightPopover();
                }
            }),
            pd, [](gpointer ptr, GClosure*) { delete static_cast<PresetData*>(ptr); },
            static_cast<GConnectFlags>(0));

        gtk_box_pack_start(GTK_BOX(presetsBox), pBtn, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vbox), presetsBox, FALSE, FALSE, 0);

    // 3. Continuous Slider and Stepper Row
    GtkWidget* sliderRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    double minW = (activeTool == Tool::Highlighter) ? InkingLimits::HighlighterMinWidth
                                                    : InkingLimits::PenMinWidth;
    double maxW = (activeTool == Tool::Highlighter) ? InkingLimits::HighlighterMaxWidth
                                                    : InkingLimits::PenMaxWidth;
    double stepW = (activeTool == Tool::Highlighter) ? InkingLimits::HighlighterStepWidth
                                                     : InkingLimits::PenStepWidth;

    GtkWidget* stepDownBtn = gtk_button_new_with_label("−");
    GtkStyleContext* sdCtx = gtk_widget_get_style_context(stepDownBtn);
    gtk_style_context_add_class(sdCtx, "fc-stepper-btn");
    g_signal_connect(stepDownBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self) {
                             double delta = (self->m_toolManager.activeTool() == Tool::Highlighter)
                                                ? -InkingLimits::HighlighterStepWidth
                                                : -InkingLimits::PenStepWidth;
                             self->m_toolManager.stepActiveWidth(delta);
                         }
                     }),
                     this);

    m_weightScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, minW, maxW, stepW);
    gtk_scale_set_draw_value(GTK_SCALE(m_weightScale), FALSE);
    gtk_widget_set_hexpand(m_weightScale, TRUE);
    m_updatingWeightScale = true;
    gtk_range_set_value(GTK_RANGE(m_weightScale), currentW);
    m_updatingWeightScale = false;

    g_signal_connect(m_weightScale, "value-changed",
                     G_CALLBACK(+[](GtkRange* range, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self && !self->m_updatingWeightScale) {
                             self->m_toolManager.setActiveWidth(gtk_range_get_value(range));
                         }
                     }),
                     this);

    GtkWidget* stepUpBtn = gtk_button_new_with_label("+");
    GtkStyleContext* suCtx = gtk_widget_get_style_context(stepUpBtn);
    gtk_style_context_add_class(suCtx, "fc-stepper-btn");
    g_signal_connect(stepUpBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<TopToolbarWidget*>(data);
                         if (self) {
                             double delta = (self->m_toolManager.activeTool() == Tool::Highlighter)
                                                ? InkingLimits::HighlighterStepWidth
                                                : InkingLimits::PenStepWidth;
                             self->m_toolManager.stepActiveWidth(delta);
                         }
                     }),
                     this);

    gtk_box_pack_start(GTK_BOX(sliderRow), stepDownBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sliderRow), m_weightScale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(sliderRow), stepUpBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), sliderRow, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(m_weightPopover), vbox);
    gtk_widget_show_all(vbox);
}

void TopToolbarWidget::updateUndoRedoState(bool canUndo, bool canRedo) {
    if (m_undoBtn && GTK_IS_WIDGET(m_undoBtn)) {
        gtk_widget_set_sensitive(m_undoBtn, canUndo ? TRUE : FALSE);
    }
    if (m_redoBtn && GTK_IS_WIDGET(m_redoBtn)) {
        gtk_widget_set_sensitive(m_redoBtn, canRedo ? TRUE : FALSE);
    }
    if (m_undoIcon) {
        AppIcons::setIconState(m_undoIcon, AppIcon::Undo,
                               canUndo ? IconState::Default : IconState::Disabled);
    }
    if (m_redoIcon) {
        AppIcons::setIconState(m_redoIcon, AppIcon::Redo,
                               canRedo ? IconState::Default : IconState::Disabled);
    }
}

void TopToolbarWidget::setMinimapActive(bool active) {
    if (m_minimapBtn && GTK_IS_TOGGLE_BUTTON(m_minimapBtn)) {
        const gboolean current = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_minimapBtn));
        const gboolean target = active ? TRUE : FALSE;
        if (current != target) {
            m_updatingMinimapState = true;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_minimapBtn), target);
            m_updatingMinimapState = false;
        }
    }
}

} // namespace FluidCoreApp
