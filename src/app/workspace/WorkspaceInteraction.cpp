#include "workspace/WorkspaceInteraction.h"
#include "FluidCoreEngine.h"
#include "graph/GraphTopology.h"
#include "undo/WorkspaceCommands.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/CardLayoutEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/ExcerptPayload.h"
#include "workspace/WorkspaceView.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace FluidCoreApp {

FluidCore::Rectangle WorkspaceInteraction::getMinimapRect(const WorkspaceState& state,
                                                          int viewWidth, int viewHeight) {
    const double w = std::min(state.minimapWidth, viewWidth * 0.4);
    const double h = std::min(state.minimapHeight, viewHeight * 0.4);
    const double x = viewWidth - w - state.minimapMargin;
    const double y = viewHeight - h - state.minimapMargin;
    return {x, y, w, h};
}

bool WorkspaceInteraction::minimapHitTest(const WorkspaceState& state, double screenX,
                                          double screenY, int viewWidth, int viewHeight) {
    if (!state.showMinimap)
        return false;
    const FluidCore::Rectangle r = getMinimapRect(state, viewWidth, viewHeight);
    return screenX >= r.x && screenX <= r.x + r.w && screenY >= r.y && screenY <= r.y + r.h;
}

void WorkspaceInteraction::handleMinimapInteraction(WorkspaceState& state,
                                                    FluidCore::FluidCoreAPI& api, GtkWidget* area,
                                                    double screenX, double screenY, int viewWidth,
                                                    int viewHeight) {
    const FluidCore::Rectangle mm = getMinimapRect(state, viewWidth, viewHeight);
    if (mm.w <= 0.0 || mm.h <= 0.0)
        return;

    FluidCore::Rectangle wsBounds = api.getWorkspaceBounds();
    const double currentViewW = viewWidth / state.viewport.zoom;
    const double currentViewH = viewHeight / state.viewport.zoom;

    if (wsBounds.w <= 0.0 || wsBounds.h <= 0.0) {
        wsBounds = {state.viewport.originX, state.viewport.originY, currentViewW, currentViewH};
    } else {
        const double minX = std::min(wsBounds.x, state.viewport.originX);
        const double minY = std::min(wsBounds.y, state.viewport.originY);
        const double maxX =
            std::max(wsBounds.x + wsBounds.w, state.viewport.originX + currentViewW);
        const double maxY =
            std::max(wsBounds.y + wsBounds.h, state.viewport.originY + currentViewH);
        wsBounds = {minX, minY, maxX - minX, maxY - minY};
    }

    const double padX = std::max(wsBounds.w * 0.1, 100.0);
    const double padY = std::max(wsBounds.h * 0.1, 100.0);
    const double mapWorldX = wsBounds.x - padX;
    const double mapWorldY = wsBounds.y - padY;
    const double mapWorldW = wsBounds.w + padX * 2.0;
    const double mapWorldH = wsBounds.h + padY * 2.0;

    const double nx = std::clamp((screenX - (mm.x + 8.0)) / (mm.w - 16.0), 0.0, 1.0);
    const double ny = std::clamp((screenY - (mm.y + 24.0)) / (mm.h - 32.0), 0.0, 1.0);

    const double targetCenterX = mapWorldX + nx * mapWorldW;
    const double targetCenterY = mapWorldY + ny * mapWorldH;

    state.viewport.originX = targetCenterX - currentViewW / 2.0;
    state.viewport.originY = targetCenterY - currentViewH / 2.0;

    if (area && GTK_IS_WIDGET(area)) {
        gtk_widget_queue_draw(area);
    }
}

const FluidCore::WorkspaceNode* WorkspaceInteraction::hitTestChildNodeAtWorldPoint(
    FluidCore::FluidCoreAPI& api, const FluidCore::Point& worldPt, std::string* outParentStackId) {
    const FluidCore::Rectangle queryRect{worldPt.x - 1.0, worldPt.y - 1.0, 2.0, 2.0};
    auto hits = api.queryVisibleNodes(queryRect);
    for (const auto* node : hits) {
        if (const auto* stack = dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            if (!stack->isCollapsed()) {
                const auto& children = stack->children();
                for (auto it = children.rbegin(); it != children.rend(); ++it) {
                    if (*it) {
                        const auto b = (*it)->bounds();
                        if (worldPt.x >= b.x && worldPt.x <= b.x + b.w && worldPt.y >= b.y &&
                            worldPt.y <= b.y + b.h) {
                            if (outParentStackId) {
                                *outParentStackId = stack->id();
                            }
                            return it->get();
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

const FluidCore::WorkspaceNode*
WorkspaceInteraction::hitTestNodeAtWorldPoint(FluidCore::FluidCoreAPI& api,
                                              const FluidCore::Point& worldPt) {
    const FluidCore::Rectangle queryRect{worldPt.x - 1.0, worldPt.y - 1.0, 2.0, 2.0};
    auto hits = api.queryVisibleNodes(queryRect);
    for (const auto* node : hits) {
        if (dynamic_cast<const FluidCore::ExcerptCardNode*>(node) ||
            dynamic_cast<const FluidCore::CardStackNode*>(node)) {
            const auto b = node->bounds();
            if (worldPt.x >= b.x && worldPt.x <= b.x + b.w && worldPt.y >= b.y &&
                worldPt.y <= b.y + b.h) {
                return node;
            }
        }
    }
    for (const auto* node : hits) {
        if (!dynamic_cast<const FluidCore::CanvasStrokeNode*>(node)) {
            const auto b = node->bounds();
            if (worldPt.x >= b.x && worldPt.x <= b.x + b.w && worldPt.y >= b.y &&
                worldPt.y <= b.y + b.h) {
                return node;
            }
        }
    }
    return nullptr;
}

std::string WorkspaceInteraction::hitTestEdgeAtWorldPoint(FluidCore::FluidCoreAPI& api,
                                                          const FluidCore::Point& worldPt,
                                                          double tolerance) {
    const auto edgeIds = api.getAllEdges();
    for (const auto& eid : edgeIds) {
        FluidCore::BezierSpline spline = api.getEdgeGeometry(eid);
        if (FluidCore::GraphTopology::hitTestSpline(spline, worldPt, tolerance)) {
            return eid;
        }
    }
    return "";
}

void WorkspaceInteraction::showEdgeContextMenu(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                               GtkWidget* area, const std::string& edgeId,
                                               GdkEventButton* event) {
    state.selectedEdgeId = edgeId;
    state.selectedNodeId.reset();
    if (area && GTK_IS_WIDGET(area)) {
        gtk_widget_queue_draw(area);
    }

    struct MenuContext {
        WorkspaceState* state;
        FluidCore::FluidCoreAPI* api;
        GtkWidget* area;
    };
    auto* ctx = new MenuContext{&state, &api, area};

    GtkWidget* menu = gtk_menu_new();
    GtkWidget* deleteItem = gtk_menu_item_new_with_label("Delete Connector");
    g_signal_connect_data(
        deleteItem, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            auto* c = static_cast<MenuContext*>(data);
            if (c && c->state && c->state->selectedEdgeId) {
                auto* engine = dynamic_cast<FluidCore::FluidCoreEngine*>(c->api);
                WorkspaceView* wsView = (c->area && GTK_IS_WIDGET(c->area))
                                            ? static_cast<WorkspaceView*>(g_object_get_data(
                                                  G_OBJECT(c->area), "workspace-view-instance"))
                                            : nullptr;
                if (wsView && engine) {
                    wsView->undoStack().pushAndExecute(
                        std::make_unique<FluidCore::RemoveEdgeCommand>(engine->graphTopology(),
                                                                       *c->state->selectedEdgeId));
                } else {
                    c->api->removeEdge(*c->state->selectedEdgeId);
                }
                c->state->selectedEdgeId.reset();
                if (c->area && GTK_IS_WIDGET(c->area)) {
                    gtk_widget_queue_draw(c->area);
                }
            }
        }),
        ctx, [](gpointer data, GClosure*) { delete static_cast<MenuContext*>(data); },
        static_cast<GConnectFlags>(0));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), deleteItem);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

void WorkspaceInteraction::showNodeContextMenu(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                               GtkWidget* area,
                                               const FluidCore::WorkspaceNode* node,
                                               const std::string& parentStackId,
                                               GdkEventButton* event) {
    if (!node)
        return;
    const std::string nodeId = node->id();
    state.selectedNodeId = nodeId;
    state.selectedEdgeId.reset();
    if (area && GTK_IS_WIDGET(area)) {
        gtk_widget_queue_draw(area);
    }

    struct MenuContext {
        WorkspaceState* state;
        FluidCore::FluidCoreAPI* api;
        GtkWidget* area;
        std::string nodeId;
        std::string parentStackId;
    };
    auto* ctx = new MenuContext{&state, &api, area, nodeId, parentStackId};

    GtkWidget* menu = gtk_menu_new();
    const bool isStack = (dynamic_cast<const FluidCore::CardStackNode*>(node) != nullptr);

    if (isStack) {
        GtkWidget* renameItem = gtk_menu_item_new_with_label("Rename Stack…");
        g_signal_connect_data(
            renameItem, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
                auto* c = static_cast<MenuContext*>(data);
                if (c && c->area) {
                    auto* renameFn = static_cast<std::function<void(const std::string&)>*>(
                        g_object_get_data(G_OBJECT(c->area), "workspace-rename-handler"));
                    if (renameFn) {
                        (*renameFn)(c->nodeId);
                    } else {
                        promptRenameStack(*c->api, c->area, c->nodeId);
                    }
                }
            }),
            ctx, nullptr, static_cast<GConnectFlags>(0));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), renameItem);

        GtkWidget* sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
    }

    GtkWidget* deleteItem = gtk_menu_item_new_with_label(isStack ? "Delete Stack" : "Delete Card");
    g_signal_connect_data(
        deleteItem, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            auto* c = static_cast<MenuContext*>(data);
            if (c && c->state) {
                auto* engine = dynamic_cast<FluidCore::FluidCoreEngine*>(c->api);
                WorkspaceView* wsView = (c->area && GTK_IS_WIDGET(c->area))
                                            ? static_cast<WorkspaceView*>(g_object_get_data(
                                                  G_OBJECT(c->area), "workspace-view-instance"))
                                            : nullptr;
                if (wsView && engine) {
                    wsView->undoStack().beginMacro("Delete Node");
                    if (!c->parentStackId.empty()) {
                        wsView->undoStack().pushAndExecute(
                            std::make_unique<FluidCore::ExtractChildCommand>(
                                engine->workspaceModel(), c->parentStackId, c->nodeId,
                                FluidCore::Point{0, 0}));
                    }
                    for (const auto& edgeId : c->api->getConnectedEdges(c->nodeId)) {
                        wsView->undoStack().pushAndExecute(
                            std::make_unique<FluidCore::RemoveEdgeCommand>(engine->graphTopology(),
                                                                           edgeId));
                    }
                    wsView->undoStack().pushAndExecute(
                        std::make_unique<FluidCore::RemoveNodeCommand>(engine->workspaceModel(),
                                                                       c->nodeId));
                    wsView->undoStack().endMacro();
                } else {
                    if (!c->parentStackId.empty()) {
                        c->api->extractChildFromStack(c->parentStackId, c->nodeId, {0, 0});
                    }
                    c->api->removeNode(c->nodeId);
                }
                c->state->selectedNodeId.reset();
                if (c->area && GTK_IS_WIDGET(c->area)) {
                    gtk_widget_queue_draw(c->area);
                }
            }
        }),
        ctx, [](gpointer data, GClosure*) { delete static_cast<MenuContext*>(data); },
        static_cast<GConnectFlags>(0));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), deleteItem);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

void WorkspaceInteraction::promptRenameStack(FluidCore::FluidCoreAPI& api, GtkWidget* area,
                                             const std::string& stackId) {
    if (area && GTK_IS_WIDGET(area)) {
        auto* renameFn = static_cast<std::function<void(const std::string&)>*>(
            g_object_get_data(G_OBJECT(area), "workspace-rename-handler"));
        if (renameFn) {
            (*renameFn)(stackId);
            return;
        }
    }

    GtkWidget* toplevel = area ? gtk_widget_get_toplevel(area) : nullptr;
    GtkWindow* parentWin = (toplevel && GTK_IS_WINDOW(toplevel)) ? GTK_WINDOW(toplevel) : nullptr;

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Rename Stack", parentWin,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_Rename", GTK_RESPONSE_ACCEPT, nullptr);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 14);
    gtk_box_set_spacing(GTK_BOX(contentArea), 8);

    GtkWidget* label = gtk_label_new("Enter a new name for this card stack:");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_pack_start(GTK_BOX(contentArea), label, FALSE, FALSE, 0);

    GtkWidget* entry = gtk_entry_new();
    const std::string currentTitle = api.getStackTitle(stackId);
    gtk_entry_set_text(GTK_ENTRY(entry), currentTitle.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(contentArea), entry, FALSE, FALSE, 0);

    struct DialogCtx {
        FluidCore::FluidCoreAPI* api;
        GtkWidget* area;
        GtkWidget* entry;
        std::string stackId;
    };
    auto* ctx = new DialogCtx{&api, area, entry, stackId};

    g_signal_connect_data(
        dialog, "response", G_CALLBACK(+[](GtkDialog* dlg, gint response, gpointer data) {
            auto* c = static_cast<DialogCtx*>(data);
            if (c && response == GTK_RESPONSE_ACCEPT) {
                const gchar* newText = gtk_entry_get_text(GTK_ENTRY(c->entry));
                if (newText && newText[0] != '\0') {
                    c->api->setStackTitle(c->stackId, std::string(newText));
                }
            }
            if (c && c->area && GTK_IS_WIDGET(c->area)) {
                gtk_widget_queue_draw(c->area);
            }
            gtk_widget_destroy(GTK_WIDGET(dlg));
        }),
        ctx, [](gpointer data, GClosure*) { delete static_cast<DialogCtx*>(data); },
        static_cast<GConnectFlags>(0));

    gtk_widget_show_all(dialog);
}

void WorkspaceInteraction::handleExcerptDrop(WorkspaceState& state, FluidCore::FluidCoreAPI& api,
                                             GtkWidget* area, GdkDragContext* context, gint x,
                                             gint y, GtkSelectionData* data, guint info, guint time,
                                             const ExcerptAddedCallback& excerptAddedCb) {
    (void)info;
    state.isDropHovering = false;

    const guchar* rawData = gtk_selection_data_get_data(data);
    const gint len = gtk_selection_data_get_length(data);
    if (!rawData || len <= 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    FluidCore::Point dropWorld = state.viewport.screenToWorld(x, y);

    std::optional<FluidCore::ExcerptDropPayload> payloadOpt =
        FluidCore::deserializeExcerptPayload(reinterpret_cast<const uint8_t*>(rawData), len);

    if (!payloadOpt.has_value()) {
        guchar* textData = gtk_selection_data_get_text(data);
        if (textData) {
            FluidCore::ExcerptDropPayload fallbackPayload;
            fallbackPayload.sourceDocId = "clipboard";
            fallbackPayload.sourcePageNo = 0;
            fallbackPayload.sourceNormalizedRect = {0.0, 0.0, 1.0, 1.0};
            fallbackPayload.textSnippet = reinterpret_cast<char*>(textData);
            fallbackPayload.isImageExcerpt = false;
            fallbackPayload.color = {255, 220, 0, 255};
            payloadOpt = fallbackPayload;
            g_free(textData);
        }
    }

    if (payloadOpt.has_value()) {
        const auto& payload = *payloadOpt;
        static std::atomic<uint64_t> s_excerptSeq{0};
        const uint64_t ts = static_cast<uint64_t>(g_get_real_time());
        std::string cardId =
            "excerpt-" + std::to_string(ts) + "-" + std::to_string(++s_excerptSeq);

        const auto [cardW, cardH] =
            FluidCore::CardLayoutEngine::computeExcerptCardDimensions(payload);
        const double safeW = (std::isnan(cardW) || cardW < 60.0) ? 240.0 : std::min(cardW, 2000.0);
        const double safeH = (std::isnan(cardH) || cardH < 40.0) ? 160.0 : std::min(cardH, 2000.0);
        FluidCore::Rectangle cardBounds{dropWorld.x, dropWorld.y, safeW, safeH};
        uint64_t timestamp = static_cast<uint64_t>(time);

        auto card = std::make_unique<FluidCore::ExcerptCardNode>(
            cardId, cardBounds, payload.sourceDocId, payload.sourcePageNo,
            payload.sourceNormalizedRect, payload.textSnippet, payload.isImageExcerpt,
            payload.color, timestamp);

        if (excerptAddedCb) {
            excerptAddedCb(*card);
        }

        auto* engine = dynamic_cast<FluidCore::FluidCoreEngine*>(&api);
        WorkspaceView* wsView = (area && GTK_IS_WIDGET(area))
                                    ? static_cast<WorkspaceView*>(g_object_get_data(
                                          G_OBJECT(area), "workspace-view-instance"))
                                    : nullptr;

        if (wsView && engine) {
            wsView->undoStack().pushAndExecute(std::make_unique<FluidCore::InsertNodeCommand>(
                engine->workspaceModel(), std::move(card)));
        } else {
            api.insertNode(std::move(card));
        }
        if (area && GTK_IS_WIDGET(area)) {
            gtk_widget_queue_draw(area);
        }
        gtk_drag_finish(context, TRUE, FALSE, time);
        return;
    }

    gtk_drag_finish(context, FALSE, FALSE, time);
}

} // namespace FluidCoreApp
