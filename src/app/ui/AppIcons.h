#pragma once

#include <gtk/gtk.h>

namespace FluidCoreApp {

enum class AppIcon {
    // Drawing & Selection Tools
    Select,
    Pen,
    Highlighter,
    Eraser,
    Crop,
    Link,

    // History & Navigation
    Undo,
    Redo,
    ZoomIn,
    ZoomOut,
    ResetView,
    Minimap,

    // Global Actions
    Search,
    Export,

    // Header Bar Actions
    NewFile,
    OpenFile,
    SaveFile
};

enum class IconState {
    Default, // #334155 (slate-700)
    Active,  // #ffffff (white)
    Disabled // #94a3b8 (slate-400)
};

namespace AppIcons {

constexpr int ToolbarSize = 16;
constexpr int HeaderSize = 16;
constexpr int SmallSize = 14;

// Creates a new GtkImage widget displaying the specified icon and state.
// Rendered GdkPixbuf instances are cached lazily, while each call returns a distinct GtkWidget*.
GtkWidget* createIconWidget(AppIcon icon, int size = ToolbarSize,
                            IconState state = IconState::Default);

// Updates an existing GtkImage widget to display a different state of the icon.
void setIconState(GtkWidget* imageWidget, AppIcon icon, IconState state, int size = ToolbarSize);

// Retrieves the cached GdkPixbuf* for an icon (or renders and caches it on demand).
GdkPixbuf* getCachedPixbuf(AppIcon icon, int size = ToolbarSize,
                           IconState state = IconState::Default);

} // namespace AppIcons

} // namespace FluidCoreApp
