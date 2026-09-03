#pragma once

#include <functional>
#include <gtk/gtk.h>
#include <string>

namespace FluidCoreApp {

enum class SaveStatus { Saved, Unsaved, Failed };

// Production GTK3 HeaderBar titlebar for FluidCore window chrome (TASK-5.2).
// Integrates project identity, dirty-state monitoring badge, and native action triggers.
class AppHeaderBar {
  public:
    explicit AppHeaderBar(GtkWindow* parentWindow);
    ~AppHeaderBar();

    AppHeaderBar(const AppHeaderBar&) = delete;
    AppHeaderBar& operator=(const AppHeaderBar&) = delete;

    GtkWidget* widget() const { return m_headerBar; }

    void setProjectTitle(const std::string& title, const std::string& subtitle = "");
    void setSaveStatus(SaveStatus status);

    void setOnNewProject(std::function<void()> cb) { m_onNewProject = std::move(cb); }
    void setOnOpenPdf(std::function<void()> cb) { m_onOpenPdf = std::move(cb); }
    void setOnOpenProject(std::function<void()> cb) { m_onOpenProject = std::move(cb); }
    void setOnSaveProject(std::function<void()> cb) { m_onSaveProject = std::move(cb); }
    void setOnSaveProjectAs(std::function<void()> cb) { m_onSaveProject = std::move(cb); }
    void setOnExport(std::function<void()> cb) { m_onExport = std::move(cb); }

  private:
    void setupStyles();
    void createWidgets(GtkWindow* parentWindow);

    GtkWidget* m_headerBar = nullptr;
    GtkWidget* m_titleBox = nullptr;
    GtkWidget* m_titleLabel = nullptr;
    GtkWidget* m_subtitleLabel = nullptr;
    GtkWidget* m_statusBadge = nullptr;

    // Action buttons
    GtkWidget* m_newBtn = nullptr;
    GtkWidget* m_openMenuBtn = nullptr;
    GtkWidget* m_openMenu = nullptr;
    GtkWidget* m_saveBtn = nullptr;
    GtkWidget* m_exportBtn = nullptr;

    // Callbacks
    std::function<void()> m_onNewProject;
    std::function<void()> m_onOpenPdf;
    std::function<void()> m_onOpenProject;
    std::function<void()> m_onSaveProject;
    std::function<void()> m_onExport;
};

} // namespace FluidCoreApp
