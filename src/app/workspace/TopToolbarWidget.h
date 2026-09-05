#pragma once

#include "services/ToolManager.h"

#include <functional>
#include <gtk/gtk.h>

namespace FluidCoreApp {

class TopToolbarWidget {
  public:
    explicit TopToolbarWidget(ToolManager& toolManager);
    ~TopToolbarWidget();

    TopToolbarWidget(const TopToolbarWidget&) = delete;
    TopToolbarWidget& operator=(const TopToolbarWidget&) = delete;

    GtkWidget* widget() const { return m_rootContainer; }

    void updateUndoRedoState(bool canUndo, bool canRedo);
    void setMinimapActive(bool active);

    void setOnUndo(std::function<void()> cb) { m_onUndo = std::move(cb); }
    void setOnRedo(std::function<void()> cb) { m_onRedo = std::move(cb); }
    void setOnZoomIn(std::function<void()> cb) { m_onZoomIn = std::move(cb); }
    void setOnZoomOut(std::function<void()> cb) { m_onZoomOut = std::move(cb); }
    void setOnResetView(std::function<void()> cb) { m_onResetView = std::move(cb); }
    void setOnToggleMinimap(std::function<void()> cb) { m_onToggleMinimap = std::move(cb); }
    void setOnSearch(std::function<void()> cb) { m_onSearch = std::move(cb); }
    void setOnExport(std::function<void()> cb) { m_onExport = std::move(cb); }

  private:
    void setupStyles();
    void createWidgets();
    void onToolStateChanged(Tool tool);

    ToolManager& m_toolManager;
    bool m_updatingToolUI = false;

    GtkWidget* m_rootContainer = nullptr;
    GtkWidget* m_pillBox = nullptr;

    // Tool toggle buttons & icon widgets
    GtkWidget* m_selectBtn = nullptr;
    GtkWidget* m_selectIcon = nullptr;
    GtkWidget* m_penBtn = nullptr;
    GtkWidget* m_penIcon = nullptr;
    GtkWidget* m_highlighterBtn = nullptr;
    GtkWidget* m_highlighterIcon = nullptr;
    GtkWidget* m_eraserBtn = nullptr;
    GtkWidget* m_eraserIcon = nullptr;
    GtkWidget* m_cropBtn = nullptr;
    GtkWidget* m_cropIcon = nullptr;
    GtkWidget* m_connectorBtn = nullptr;
    GtkWidget* m_connectorIcon = nullptr;

    // History action buttons & icon widgets
    GtkWidget* m_undoBtn = nullptr;
    GtkWidget* m_undoIcon = nullptr;
    GtkWidget* m_redoBtn = nullptr;
    GtkWidget* m_redoIcon = nullptr;

    // Navigation & View action buttons
    GtkWidget* m_zoomInBtn = nullptr;
    GtkWidget* m_zoomOutBtn = nullptr;
    GtkWidget* m_resetViewBtn = nullptr;
    GtkWidget* m_minimapBtn = nullptr;

    // Utility action buttons
    GtkWidget* m_searchBtn = nullptr;
    GtkWidget* m_exportBtn = nullptr;

    // Callbacks
    std::function<void()> m_onUndo;
    std::function<void()> m_onRedo;
    std::function<void()> m_onZoomIn;
    std::function<void()> m_onZoomOut;
    std::function<void()> m_onResetView;
    std::function<void()> m_onToggleMinimap;
    std::function<void()> m_onSearch;
    std::function<void()> m_onExport;
};

} // namespace FluidCoreApp
