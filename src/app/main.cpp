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

void onActivate(GtkApplication* app, gpointer userData) {
    auto* api = static_cast<FluidCoreAPI*>(userData);

    // Widgets may only be created after gtk_init(), which happens inside
    // g_application_run() — so WorkspaceView is built here, not in main().
    auto* workspace = new FluidCoreApp::WorkspaceView(*api);
    g_object_set_data_full(G_OBJECT(app), "workspace-view", workspace,
                           +[](gpointer data) {
                               delete static_cast<FluidCoreApp::WorkspaceView*>(data);
                           });

    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "FluidCore");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* documentPane = gtk_label_new("Document pane (Wave 2: Xournal++ host)");
    gtk_widget_set_size_request(documentPane, 360, -1);
    gtk_paned_pack1(GTK_PANED(paned), documentPane, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), workspace->widget(), TRUE, TRUE);

    gtk_container_add(GTK_CONTAINER(window), paned);
    gtk_widget_show_all(window);
}

} // namespace

int main(int argc, char** argv) {
    FluidCoreEngine engine("default-project");
    seedDemoContent(engine);

    GtkApplication* app = gtk_application_new("org.fluidcore.platform", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &engine);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
