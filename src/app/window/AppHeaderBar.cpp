#include "window/AppHeaderBar.h"

namespace FluidCoreApp {

namespace {

GtkWidget* createHeaderButton(const char* label, const char* tooltip, bool isPrimary = false) {
    GtkWidget* btn = gtk_button_new_with_label(label);
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_set_can_focus(btn, FALSE);
    GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(ctx, "fc-header-btn");
    if (isPrimary) {
        gtk_style_context_add_class(ctx, "fc-header-btn-primary");
    }
    return btn;
}

} // namespace

AppHeaderBar::AppHeaderBar(GtkWindow* parentWindow) {
    setupStyles();
    createWidgets(parentWindow);
}

AppHeaderBar::~AppHeaderBar() = default;

void AppHeaderBar::setupStyles() {
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = "window, window.csd, window decoration, window.csd decoration {"
                      "  box-shadow: none;"
                      "  margin: 0;"
                      "  padding: 0;"
                      "  border: none;"
                      "}"
                      ".fc-header-bar {"
                      "  background: #0f172a;"
                      "  border-bottom: 1px solid #1e293b;"
                      "  min-height: 44px;"
                      "  padding: 4px 10px;"
                      "}"
                      ".fc-header-btn {"
                      "  min-height: 28px;"
                      "  padding: 3px 10px;"
                      "  margin: 0 2px;"
                      "  border-radius: 6px;"
                      "  border: 1px solid #334155;"
                      "  background: #1e293b;"
                      "  color: #f8fafc;"
                      "  font-weight: 500;"
                      "  font-size: 12px;"
                      "}"
                      ".fc-header-btn:hover {"
                      "  background: #334155;"
                      "  border-color: #475569;"
                      "  color: #ffffff;"
                      "}"
                      ".fc-header-btn:active {"
                      "  background: #2563eb;"
                      "  border-color: #1d4ed8;"
                      "  color: #ffffff;"
                      "}"
                      ".fc-header-btn-primary {"
                      "  background: #2563eb;"
                      "  border-color: #1d4ed8;"
                      "  color: #ffffff;"
                      "}"
                      ".fc-header-btn-primary:hover {"
                      "  background: #1d4ed8;"
                      "  border-color: #1e40af;"
                      "}"
                      ".fc-title-main {"
                      "  font-weight: 700;"
                      "  font-size: 13px;"
                      "  color: #f8fafc;"
                      "}"
                      ".fc-title-sub {"
                      "  font-size: 10px;"
                      "  color: #94a3b8;"
                      "}"
                      ".fc-save-status-pill {"
                      "  border-radius: 12px;"
                      "  padding: 2px 8px;"
                      "  font-size: 10px;"
                      "  font-weight: 600;"
                      "  margin-left: 6px;"
                      "}"
                      ".fc-status-saved {"
                      "  background-color: rgba(16, 185, 129, 0.15);"
                      "  color: #10b981;"
                      "  border: 1px solid rgba(16, 185, 129, 0.35);"
                      "}"
                      ".fc-status-unsaved {"
                      "  background-color: rgba(245, 158, 11, 0.15);"
                      "  color: #f59e0b;"
                      "  border: 1px solid rgba(245, 158, 11, 0.35);"
                      "}"
                      ".fc-status-failed {"
                      "  background-color: rgba(239, 68, 68, 0.15);"
                      "  color: #ef4444;"
                      "  border: 1px solid rgba(239, 68, 68, 0.35);"
                      "}";

    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void AppHeaderBar::createWidgets(GtkWindow* /*parentWindow*/) {
    m_headerBar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(m_headerBar), FALSE);
    GtkStyleContext* headerCtx = gtk_widget_get_style_context(m_headerBar);
    gtk_style_context_add_class(headerCtx, "fc-header-bar");

    // Title and Subtitle Container
    m_titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(m_titleBox, GTK_ALIGN_CENTER);

    GtkWidget* mainRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(mainRow, GTK_ALIGN_CENTER);

    m_titleLabel = gtk_label_new("FluidCore — Untitled Project");
    GtkStyleContext* titleCtx = gtk_widget_get_style_context(m_titleLabel);
    gtk_style_context_add_class(titleCtx, "fc-title-main");
    gtk_box_pack_start(GTK_BOX(mainRow), m_titleLabel, FALSE, FALSE, 0);

    m_statusBadge = gtk_label_new("● Saved");
    GtkStyleContext* badgeCtx = gtk_widget_get_style_context(m_statusBadge);
    gtk_style_context_add_class(badgeCtx, "fc-save-status-pill");
    gtk_style_context_add_class(badgeCtx, "fc-status-saved");
    gtk_box_pack_start(GTK_BOX(mainRow), m_statusBadge, FALSE, FALSE, 0);

    m_subtitleLabel = gtk_label_new("Workspace Canvas");
    GtkStyleContext* subCtx = gtk_widget_get_style_context(m_subtitleLabel);
    gtk_style_context_add_class(subCtx, "fc-title-sub");
    gtk_widget_set_halign(m_subtitleLabel, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(m_titleBox), mainRow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_titleBox), m_subtitleLabel, FALSE, FALSE, 0);
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(m_headerBar), m_titleBox);

    // Left Action Cluster: New, Open Menu, Save, Save As
    GtkWidget* leftCluster = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    m_newBtn = createHeaderButton("🗁 New", "New Project (Ctrl+N)");
    g_signal_connect_swapped(m_newBtn, "clicked", G_CALLBACK(+[](AppHeaderBar* self) {
                                 if (self && self->m_onNewProject)
                                     self->m_onNewProject();
                             }),
                             this);
    gtk_box_pack_start(GTK_BOX(leftCluster), m_newBtn, FALSE, FALSE, 0);

    // Open Menu Button
    m_openMenuBtn = gtk_menu_button_new();
    gtk_widget_set_tooltip_text(m_openMenuBtn, "Open PDF or Project (Ctrl+O / Ctrl+Shift+O)");
    gtk_button_set_label(GTK_BUTTON(m_openMenuBtn), "📂 Open ▾");
    GtkStyleContext* openBtnCtx = gtk_widget_get_style_context(m_openMenuBtn);
    gtk_style_context_add_class(openBtnCtx, "fc-header-btn");

    m_openMenu = gtk_menu_new();

    GtkWidget* openPdfItem = gtk_menu_item_new_with_label("Open PDF... (Ctrl+O)");
    g_signal_connect_swapped(openPdfItem, "activate", G_CALLBACK(+[](AppHeaderBar* self) {
                                 if (self && self->m_onOpenPdf)
                                     self->m_onOpenPdf();
                             }),
                             this);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_openMenu), openPdfItem);

    GtkWidget* openProjItem =
        gtk_menu_item_new_with_label("Open Project (.ltproj)... (Ctrl+Shift+O)");
    g_signal_connect_swapped(openProjItem, "activate", G_CALLBACK(+[](AppHeaderBar* self) {
                                 if (self && self->m_onOpenProject)
                                     self->m_onOpenProject();
                             }),
                             this);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_openMenu), openProjItem);

    gtk_widget_show_all(m_openMenu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(m_openMenuBtn), m_openMenu);
    gtk_box_pack_start(GTK_BOX(leftCluster), m_openMenuBtn, FALSE, FALSE, 0);

    m_saveBtn = createHeaderButton("💾 Save", "Save Project (.ltproj Bundle) (Ctrl+S)", true);
    g_signal_connect_swapped(m_saveBtn, "clicked", G_CALLBACK(+[](AppHeaderBar* self) {
                                 if (self && self->m_onSaveProject)
                                     self->m_onSaveProject();
                             }),
                             this);
    gtk_box_pack_start(GTK_BOX(leftCluster), m_saveBtn, FALSE, FALSE, 0);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(m_headerBar), leftCluster);

    // Right Action Cluster: Export
    GtkWidget* rightCluster = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    m_exportBtn =
        createHeaderButton("📤 Export", "Export Annotated PDF / Markdown Outline (Ctrl+E)");
    g_signal_connect_swapped(m_exportBtn, "clicked", G_CALLBACK(+[](AppHeaderBar* self) {
                                 if (self && self->m_onExport)
                                     self->m_onExport();
                             }),
                             this);
    gtk_box_pack_start(GTK_BOX(rightCluster), m_exportBtn, FALSE, FALSE, 0);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(m_headerBar), rightCluster);

    gtk_widget_show_all(m_headerBar);
}

void AppHeaderBar::setProjectTitle(const std::string& title, const std::string& subtitle) {
    if (m_titleLabel) {
        std::string fullTitle = "FluidCore — " + (title.empty() ? "Untitled Project" : title);
        gtk_label_set_text(GTK_LABEL(m_titleLabel), fullTitle.c_str());
    }
    if (m_subtitleLabel) {
        gtk_label_set_text(GTK_LABEL(m_subtitleLabel),
                           subtitle.empty() ? "Workspace Canvas" : subtitle.c_str());
    }
}

void AppHeaderBar::setSaveStatus(SaveStatus status) {
    if (!m_statusBadge || !GTK_IS_WIDGET(m_statusBadge) || !GTK_IS_LABEL(m_statusBadge))
        return;

    GtkStyleContext* ctx = gtk_widget_get_style_context(m_statusBadge);
    if (!ctx || !GTK_IS_STYLE_CONTEXT(ctx))
        return;

    gtk_style_context_remove_class(ctx, "fc-status-saved");
    gtk_style_context_remove_class(ctx, "fc-status-unsaved");
    gtk_style_context_remove_class(ctx, "fc-status-failed");

    switch (status) {
    case SaveStatus::Saved:
        gtk_label_set_text(GTK_LABEL(m_statusBadge), "● Saved");
        gtk_style_context_add_class(ctx, "fc-status-saved");
        break;
    case SaveStatus::Unsaved:
        gtk_label_set_text(GTK_LABEL(m_statusBadge), "● Unsaved Changes");
        gtk_style_context_add_class(ctx, "fc-status-unsaved");
        break;
    case SaveStatus::Failed:
        gtk_label_set_text(GTK_LABEL(m_statusBadge), "● Save Failed");
        gtk_style_context_add_class(ctx, "fc-status-failed");
        break;
    }
}

} // namespace FluidCoreApp
