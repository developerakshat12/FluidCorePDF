#include "DocumentPane.h"
#include "FluidCoreEngine.h"
#include "WorkspaceView.h"

#include <gtk/gtk.h>

#include <memory>
#include <string>
#include <utility>

namespace {

using FluidCore::FluidCoreAPI;
using FluidCore::FluidCoreEngine;
using FluidCore::Rectangle;

// Minimal concrete node so the demo shell can seed content through the abstract
// boundary. Real node types (excerpt cards, notes, stacks) arrive with M1+.
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
    api.insertNode(
        std::make_unique<SampleNode>("excerpt-pdf-1", Rectangle{80.0, 80.0, 220.0, 160.0}));
    api.insertNode(std::make_unique<SampleNode>("note-a", Rectangle{380.0, 140.0, 160.0, 100.0}));
    api.insertNode(std::make_unique<SampleNode>("note-b", Rectangle{260.0, 340.0, 200.0, 120.0}));
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

    const gchar* accels[] = {"<Primary>s", "<Control>s", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.save", accels);

    // Direct key-press fallback for window-level shortcut handling
    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         if ((event->state & GDK_CONTROL_MASK) &&
                             (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
                             auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                             if (pane) {
                                 pane->save();
                             }
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     documentPane);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* documentWidget = documentPane->widget();
    gtk_widget_set_size_request(documentWidget, 360, -1);
    gtk_paned_pack1(GTK_PANED(paned), documentWidget, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), workspace->widget(), TRUE, TRUE);
    // GtkPaned defaults to a collapsed divider (position 0); open the split so
    // the workspace canvas is visible without a manual drag.
    gtk_paned_set_position(GTK_PANED(paned), 480);

    gtk_container_add(GTK_CONTAINER(window), paned);
    gtk_widget_show_all(window);
}

} // namespace

int main(int argc, char** argv) {
    FluidCoreEngine engine("default-project");
    seedDemoContent(engine);

    // Capture before g_application_run(): GApplication may consume argv.
    const std::string pdfPath = argc > 1 ? argv[1] : "";
    AppContext context{&engine, &pdfPath};

    GtkApplication* app = gtk_application_new("org.fluidcore.platform", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &context);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
