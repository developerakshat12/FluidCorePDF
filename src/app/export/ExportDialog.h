#pragma once

#include "FluidCoreAPI.h"
#include "document/DocumentPane.h"
#include "workspace/WorkspaceView.h"

#include <gtk/gtk.h>

namespace FluidCoreApp {

// Modern export dialog supporting Flattened Annotated PDF and Workspace Markdown Outline.
class ExportDialog {
  public:
    static void show(GtkWindow* parent, DocumentPane* pane, WorkspaceView* workspace,
                     FluidCore::FluidCoreAPI* api);
};

} // namespace FluidCoreApp
