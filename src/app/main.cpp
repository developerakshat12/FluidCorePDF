#include "DocumentPane.h"
#include "FluidCoreEngine.h"
#include "WorkspaceView.h"
#include "workspace/ExcerptCardNode.h"

#include <gtk/gtk.h>
#include <iostream>

#include <memory>
#include <string>
#include <utility>

namespace {

using FluidCore::Color;
using FluidCore::ExcerptCardNode;
using FluidCore::FluidCoreAPI;
using FluidCore::FluidCoreEngine;
using FluidCore::Rectangle;

// Minimal concrete node so the demo shell can seed generic notes alongside excerpts.
class SampleNode final : public FluidCore::WorkspaceNode {
  public:
    SampleNode(std::string id, Rectangle bounds) : m_id(std::move(id)), m_bounds(bounds) {}
    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return m_bounds; }

  private:
    std::string m_id;
    Rectangle m_bounds;
};

void seedDemoContent(FluidCoreAPI& api) {
    // Cluster 1: Primary PDF excerpts (Drag-out Excerpt Cards)
    api.insertNode(std::make_unique<ExcerptCardNode>(
        "excerpt-clause-1", Rectangle{80.0, 80.0, 260.0, 150.0}, "doc-primary.pdf", 0,
        Rectangle{0.08, 0.12, 0.84, 0.18},
        "The infinite 2D canvas provides unconstrained spatial arrangement for research synthesis "
        "and literature clustering.",
        false, Color{255, 220, 0, 255}));

    api.insertNode(std::make_unique<ExcerptCardNode>(
        "excerpt-clause-2", Rectangle{370.0, 80.0, 260.0, 150.0}, "doc-primary.pdf", 1,
        Rectangle{0.10, 0.20, 0.80, 0.22},
        "Spatial indexing with R*-tree enables O(log N) viewport culling and sub-millisecond query "
        "latencies across 100,000+ items.",
        false, Color{56, 189, 248, 255}));

    api.insertNode(std::make_unique<ExcerptCardNode>(
        "excerpt-diagram-1", Rectangle{660.0, 80.0, 280.0, 180.0}, "doc-primary.pdf", 2,
        Rectangle{0.15, 0.40, 0.70, 0.35}, "", true, Color{168, 85, 247, 255}));

    // Cluster 2: Synthesized notes
    api.insertNode(
        std::make_unique<SampleNode>("note-synthesis", Rectangle{180.0, 300.0, 260.0, 120.0}));
    api.insertNode(
        std::make_unique<SampleNode>("note-precedent", Rectangle{480.0, 300.0, 220.0, 110.0}));

    // Cluster 3: Distant comparative nodes across infinite canvas space
    api.insertNode(
        std::make_unique<SampleNode>("compare-patent-a", Rectangle{880.0, 480.0, 240.0, 150.0}));
    api.insertNode(
        std::make_unique<SampleNode>("compare-patent-b", Rectangle{1160.0, 480.0, 240.0, 150.0}));
    api.insertNode(
        std::make_unique<SampleNode>("summary-conclusion", Rectangle{540.0, 680.0, 300.0, 160.0}));
}

struct AppContext {
    FluidCoreAPI* api = nullptr;
    const std::string* pdfPath = nullptr;
};

void onActivate(GtkApplication* app, gpointer userData) {
    auto* context = static_cast<AppContext*>(userData);

    // Widgets may only be created after gtk_init(), which happens inside
    // g_application_run() — so views are built here, not in main().
    auto* documentPane = new FluidCoreApp::DocumentPane(*context->pdfPath);
    g_object_set_data_full(
        G_OBJECT(app), "document-pane", documentPane,
        +[](gpointer data) { delete static_cast<FluidCoreApp::DocumentPane*>(data); });

    auto* workspace = new FluidCoreApp::WorkspaceView(*context->api);
    g_object_set_data_full(
        G_OBJECT(app), "workspace-view", workspace,
        +[](gpointer data) { delete static_cast<FluidCoreApp::WorkspaceView*>(data); });

    // Wire Bi-Directional Anchor Navigation (TASK-3.3)
    workspace->setNavigateToSourceCallback(
        [documentPane](const std::string& /*docId*/, std::size_t pageNo,
                       const FluidCore::Rectangle& normRect, const std::string& excerptId,
                       const std::string& snippet, const FluidCore::Point& cardCenter) {
            if (documentPane) {
                documentPane->navigateToExcerptSource(pageNo, normRect, excerptId, snippet,
                                                      cardCenter);
            }
        });

    documentPane->setOnReturnToWorkspaceCallback(
        [workspace](const FluidCore::Point& originCoord, const std::string& cardId) {
            if (workspace) {
                workspace->glideToWorldCoord(originCoord.x, originCoord.y);
                workspace->flashExcerptCard(cardId);
            }
        });

    workspace->setOnExcerptAddedCallback([documentPane](const FluidCore::ExcerptCardNode& card) {
        if (documentPane) {
            documentPane->addExcerptAnchor(card);
        }
    });

    // Sync initial excerpt document source anchors into DocumentPane
    if (context->api) {
        std::vector<FluidCore::AnchorSpan> excerptAnchors;
        const auto& pages = documentPane->pages();
        for (const auto* node :
             context->api->queryVisibleNodes(FluidCore::Rectangle{-1e6, -1e6, 2e6, 2e6})) {
            auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node);
            if (excerpt && excerpt->sourcePageNo() < pages.size()) {
                const auto& page = pages[excerpt->sourcePageNo()];
                const auto& srcRect = excerpt->sourceNormalizedRect();
                double y0 = page.y + srcRect.y * page.height;
                double y1 = y0 + srcRect.h * page.height;
                excerptAnchors.push_back(FluidCore::AnchorSpan{y0, y1, "excerpt", 9});
            }
        }
        documentPane->setExcerptAnchors(std::move(excerptAnchors));
    }

    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "FluidCore");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);

    // Wire Ctrl+S accelerator to save annotations to companion .xopp file
    GSimpleAction* saveAction = g_simple_action_new("save", nullptr);
    g_signal_connect(saveAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->save();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(saveAction));

    const gchar* saveAccels[] = {"<Primary>s", "<Control>s", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.save", saveAccels);

    // Wire Ctrl+Z (Undo) action
    GSimpleAction* undoAction = g_simple_action_new("undo", nullptr);
    g_signal_connect(undoAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->undo();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(undoAction));

    const gchar* undoAccels[] = {"<Primary>z", "<Control>z", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.undo", undoAccels);

    // Wire Ctrl+Shift+Z / Ctrl+Y (Redo) action
    GSimpleAction* redoAction = g_simple_action_new("redo", nullptr);
    g_signal_connect(redoAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->redo();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(redoAction));

    const gchar* redoAccels[] = {"<Primary><Shift>z", "<Control><Shift>z", "<Primary>y",
                                 "<Control>y", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.redo", redoAccels);

    // Wire Ctrl+C (Copy) action
    GSimpleAction* copyAction = g_simple_action_new("copy", nullptr);
    g_signal_connect(copyAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->copySelection();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(copyAction));

    const gchar* copyAccels[] = {"<Primary>c", "<Control>c", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.copy", copyAccels);

    struct AppViewContext {
        FluidCoreApp::DocumentPane* pane;
        FluidCoreApp::WorkspaceView* workspace;
    };
    auto* viewCtx = new AppViewContext{documentPane, workspace};
    g_object_set_data_full(
        G_OBJECT(app), "app-view-context", viewCtx,
        +[](gpointer data) { delete static_cast<AppViewContext*>(data); });

    // Wire tool switching actions
    GSimpleAction* penAction = g_simple_action_new("tool_pen", nullptr);
    g_signal_connect(penAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx) {
                             if (ctx->pane)
                                 ctx->pane->setTool("pen");
                             if (ctx->workspace)
                                 ctx->workspace->setTool("pen");
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(penAction));
    const gchar* penAccels[] = {"<Alt>1", "F1", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_pen", penAccels);

    GSimpleAction* highAction = g_simple_action_new("tool_highlighter", nullptr);
    g_signal_connect(highAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx) {
                             if (ctx->pane)
                                 ctx->pane->setTool("highlighter");
                             if (ctx->workspace)
                                 ctx->workspace->setTool("highlighter");
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(highAction));
    const gchar* highAccels[] = {"<Alt>2", "F2", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_highlighter", highAccels);

    GSimpleAction* eraserAction = g_simple_action_new("tool_eraser", nullptr);
    g_signal_connect(eraserAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx) {
                             if (ctx->pane)
                                 ctx->pane->setTool("eraser");
                             if (ctx->workspace)
                                 ctx->workspace->setTool("eraser");
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(eraserAction));
    const gchar* eraserAccels[] = {"<Alt>3", "F3", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_eraser", eraserAccels);

    GSimpleAction* selectAction = g_simple_action_new("tool_select", nullptr);
    g_signal_connect(selectAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx) {
                             if (ctx->pane)
                                 ctx->pane->setTool("select");
                             if (ctx->workspace)
                                 ctx->workspace->setTool("select");
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(selectAction));
    const gchar* selectAccels[] = {"<Alt>4", "F4", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_select", selectAccels);

    // Wire Ctrl+Shift+0 (Reset Squeeze) action
    GSimpleAction* resetSqueezeAction = g_simple_action_new("reset_squeeze", nullptr);
    g_signal_connect(resetSqueezeAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->resetSqueeze();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(resetSqueezeAction));
    const gchar* resetSqueezeAccels[] = {"<Primary><Shift>0", "<Control><Shift>0",
                                         "<Primary><Shift>parenright", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.reset_squeeze",
                                          resetSqueezeAccels);

    // Wire Ctrl+F (Search) and Ctrl+Shift+S (Search Squeeze) actions
    GSimpleAction* searchAction = g_simple_action_new("search", nullptr);
    g_signal_connect(searchAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->openSearch(false);
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(searchAction));
    const gchar* searchAccels[] = {"<Primary>f", "<Control>f", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.search", searchAccels);

    GSimpleAction* searchSqueezeAction = g_simple_action_new("search_squeeze", nullptr);
    g_signal_connect(searchSqueezeAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->openSearch(true);
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(searchSqueezeAction));
    const gchar* searchSqueezeAccels[] = {"<Primary><Shift>s", "<Control><Shift>s",
                                          "<Primary><Shift>f", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.search_squeeze",
                                          searchSqueezeAccels);

    // Wire Ctrl+Shift+H (Highlight Squeeze / Highlight View) action
    GSimpleAction* highlightSqueezeAction = g_simple_action_new("highlight_squeeze", nullptr);
    g_signal_connect(highlightSqueezeAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->pane) {
                             ctx->pane->toggleHighlightView();
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(highlightSqueezeAction));
    const gchar* highlightSqueezeAccels[] = {"<Primary><Shift>h", "<Control><Shift>h", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.highlight_squeeze",
                                          highlightSqueezeAccels);

    // Wire Workspace canvas zoom, reset, and minimap actions
    GSimpleAction* wsZoomInAction = g_simple_action_new("ws_zoom_in", nullptr);
    g_signal_connect(wsZoomInAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ws = static_cast<FluidCoreApp::WorkspaceView*>(data);
                         if (ws) {
                             GtkAllocation alloc;
                             gtk_widget_get_allocation(ws->widget(), &alloc);
                             const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
                             const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
                             ws->zoomAt(1.2, cx, cy);
                         }
                     }),
                     workspace);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(wsZoomInAction));
    const gchar* wsZoomInAccels[] = {"<Primary>plus", "<Primary>equal", "<Primary>KP_Add", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.ws_zoom_in", wsZoomInAccels);

    GSimpleAction* wsZoomOutAction = g_simple_action_new("ws_zoom_out", nullptr);
    g_signal_connect(wsZoomOutAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ws = static_cast<FluidCoreApp::WorkspaceView*>(data);
                         if (ws) {
                             GtkAllocation alloc;
                             gtk_widget_get_allocation(ws->widget(), &alloc);
                             const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
                             const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
                             ws->zoomAt(0.8333, cx, cy);
                         }
                     }),
                     workspace);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(wsZoomOutAction));
    const gchar* wsZoomOutAccels[] = {"<Primary>minus", "<Primary>KP_Subtract", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.ws_zoom_out", wsZoomOutAccels);

    GSimpleAction* wsResetAction = g_simple_action_new("ws_reset", nullptr);
    g_signal_connect(wsResetAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ws = static_cast<FluidCoreApp::WorkspaceView*>(data);
                         if (ws) {
                             ws->resetView();
                         }
                     }),
                     workspace);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(wsResetAction));
    const gchar* wsResetAccels[] = {"<Primary>0", "<Primary>KP_0", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.ws_reset", wsResetAccels);

    GSimpleAction* wsToggleMinimapAction = g_simple_action_new("ws_toggle_minimap", nullptr);
    g_signal_connect(wsToggleMinimapAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ws = static_cast<FluidCoreApp::WorkspaceView*>(data);
                         if (ws) {
                             ws->setMinimapVisible(!ws->isMinimapVisible());
                         }
                     }),
                     workspace);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(wsToggleMinimapAction));
    const gchar* wsMinimapAccels[] = {"<Primary>m", "<Control>m", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.ws_toggle_minimap",
                                          wsMinimapAccels);

    // Direct key-press fallback for window-level shortcut handling
    g_signal_connect(
        window, "key-press-event",
        G_CALLBACK(+[](GtkWidget* windowWidget, GdkEventKey* event, gpointer data) -> gboolean {
            auto* ctx = static_cast<AppViewContext*>(data);
            if (!ctx) {
                return FALSE;
            }
            auto* pane = ctx->pane;
            auto* ws = ctx->workspace;

            const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
            const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
            const bool alt = (event->state & GDK_MOD1_MASK) != 0;

            if (ctrl && !shift && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
                if (pane) {
                    pane->openSearch(false);
                    return TRUE;
                }
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
                if (pane)
                    pane->copySelection();
                return TRUE;
            }
            if (!ctrl && !alt && event->keyval == GDK_KEY_Escape) {
                if (pane) {
                    if (pane->isSearchActive()) {
                        pane->closeSearch();
                    } else {
                        pane->clearTextSelection();
                    }
                }
                return TRUE;
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
                if (pane)
                    pane->save();
                return TRUE;
            }
            if (ctrl && shift &&
                (event->keyval == GDK_KEY_0 || event->keyval == GDK_KEY_parenright ||
                 event->keyval == GDK_KEY_KP_0 || event->keyval == GDK_KEY_r ||
                 event->keyval == GDK_KEY_R)) {
                if (pane) {
                    pane->resetSqueeze();
                    return TRUE;
                }
            }
            if (ctrl && shift && (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H)) {
                if (pane) {
                    pane->toggleHighlightView();
                    return TRUE;
                }
            }
            if (ctrl && shift && (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
                if (pane) {
                    pane->openSearch(true);
                    return TRUE;
                }
            }

            // Quick single-key tool switching when no modifier is held
            if (!ctrl && !alt && !shift) {
                GtkWidget* focusWidget = gtk_window_get_focus(GTK_WINDOW(windowWidget));
                if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
                    // Do not intercept typing in search bar
                    return FALSE;
                }

                if (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S) {
                    if (pane)
                        pane->setTool("select");
                    if (ws)
                        ws->setTool("select");
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_p || event->keyval == GDK_KEY_P) {
                    if (pane)
                        pane->setTool("pen");
                    if (ws)
                        ws->setTool("pen");
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H) {
                    if (pane)
                        pane->setTool("highlighter");
                    if (ws)
                        ws->setTool("highlighter");
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_e || event->keyval == GDK_KEY_E) {
                    if (pane)
                        pane->setTool("eraser");
                    if (ws)
                        ws->setTool("eraser");
                    return TRUE;
                }
            }

            return FALSE;
        }),
        viewCtx);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* documentWidget = documentPane->widget();
    gtk_widget_set_size_request(documentWidget, 360, -1);
    gtk_paned_pack1(GTK_PANED(paned), documentWidget, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), workspace->widget(), TRUE, TRUE);
    // GtkPaned defaults to a collapsed divider (position 0); open the split so
    // the workspace canvas is visible without a manual drag.
    gtk_paned_set_position(GTK_PANED(paned), 480);

    gtk_container_add(GTK_CONTAINER(window), paned);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_urgency_hint(GTK_WINDOW(window), TRUE);
    gtk_widget_show_all(window);
    gtk_window_deiconify(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(window));

    std::cout << "[FluidCore] Window ready and presented (" << documentPane->pages().size()
              << " pages loaded)." << std::endl;
}

std::string normalizePath(std::string path) {
    if (path.empty()) {
        return path;
    }
#ifndef G_OS_WIN32
    // If running on Linux/WSL and passed a Windows path like "D:\foo\bar.pdf"
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(path[0])));
        std::string sub = path.substr(2);
        for (char& c : sub) {
            if (c == '\\') {
                c = '/';
            }
        }
        return std::string("/mnt/") + drive + sub;
    }
#endif
    return path;
}

} // namespace

int main(int argc, char** argv) {
    FluidCoreEngine engine("default-project");
    seedDemoContent(engine);

    // Capture before g_application_run(): GApplication may consume argv.
    const std::string rawArg = argc > 1 ? argv[1] : "";
    const std::string pdfPath = normalizePath(rawArg);
    AppContext context{&engine, &pdfPath};

    std::cout << "[FluidCore] Starting application with document: "
              << (pdfPath.empty() ? "(none)" : pdfPath) << std::endl;

    GtkApplication* app = gtk_application_new("org.fluidcore.platform", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &context);
    // Pass argc=1 so GApplication does not reject positional file arguments without
    // G_APPLICATION_HANDLES_OPEN
    const int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return status;
}
