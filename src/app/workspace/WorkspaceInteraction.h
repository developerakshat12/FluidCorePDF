#pragma once

#include "FluidCoreAPI.h"
#include "workspace/CardLayoutEngine.h"
#include "workspace/WorkspaceState.h"

#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace FluidCoreApp {

class WorkspaceInteraction {
  public:
    using NavigateToSourceCallback =
        std::function<void(const std::string& docId, std::size_t pageNo,
                           const FluidCore::Rectangle& normRect, const std::string& excerptId,
                           const std::string& snippet, const FluidCore::Point& cardWorldCenter)>;
    using ExcerptAddedCallback = std::function<void(const FluidCore::ExcerptCardNode&)>;

    // Minimap calculation and interaction helpers
    static FluidCore::Rectangle getMinimapRect(const WorkspaceState& state, int viewWidth,
                                               int viewHeight);
    static bool minimapHitTest(const WorkspaceState& state, double screenX, double screenY,
                               int viewWidth, int viewHeight);
    static void handleMinimapInteraction(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                         GtkWidget* area, double screenX, double screenY,
                                         int viewWidth, int viewHeight);

    // Spatial node and edge hit testing
    static const FluidCore::WorkspaceNode* hitTestNodeAtWorldPoint(FluidCore::FluidCoreAPI& api,
                                                                   const FluidCore::Point& worldPt);
    static const FluidCore::WorkspaceNode*
    hitTestChildNodeAtWorldPoint(FluidCore::FluidCoreAPI& api, const FluidCore::Point& worldPt,
                                 std::string* outParentStackId);
    static std::string hitTestEdgeAtWorldPoint(FluidCore::FluidCoreAPI& api,
                                               const FluidCore::Point& worldPt,
                                               double tolerance = 8.0);

    // Context menus and modal dialogs
    static void showEdgeContextMenu(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                    GtkWidget* area, const std::string& edgeId,
                                    GdkEventButton* event);
    static void showNodeContextMenu(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                    GtkWidget* area, const FluidCore::WorkspaceNode* node,
                                    const std::string& parentStackId, GdkEventButton* event);
    static void promptRenameStack(FluidCore::FluidCoreAPI& api, GtkWidget* area,
                                  const std::string& stackId);

    // Drag-and-drop drop handling
    static void handleExcerptDrop(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                  GtkWidget* area, GdkDragContext* context, gint x, gint y,
                                  GtkSelectionData* data, guint info, guint time,
                                  const ExcerptAddedCallback& excerptAddedCb);
};

} // namespace FluidCoreApp
