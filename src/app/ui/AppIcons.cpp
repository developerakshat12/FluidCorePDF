#include "ui/AppIcons.h"

#include <gio/gio.h>
#include <map>
#include <string>
#include <tuple>

namespace FluidCoreApp {

namespace {

struct IconDefinition {
    AppIcon id;
    const char* innerSvg;
};

// Canonical Lucide vector definitions (viewBox 0 0 24 24, stroke-width 2, round linecap & join)
constexpr IconDefinition kIconDefinitions[] = {
    // 1. Selection Tool (Mouse Pointer)
    {AppIcon::Select,
     "<path d=\"m3 3 7.07 16.97 2.51-7.39 7.39-2.51L3 3z\"/><path d=\"m13 13 6 6\"/>"},

    // 2. Drawing Pen Tool (Stylus / Pencil)
    {AppIcon::Pen,
     "<path d=\"M17 3a2.85 2.83 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5Z\"/><path d=\"m15 5 4 4\"/>"},

    // 3. Highlighter Tool (Marker Chisel Tip)
    {AppIcon::Highlighter, "<path d=\"m9 11-6 6v3h3l6-6\"/><path d=\"m22 12-4.6 4.6a2 2 0 0 1-2.8 "
                           "0l-5.2-5.2a2 2 0 0 1 0-2.8L14 4\"/>"},

    // 4. Stroke Eraser Tool
    {AppIcon::Eraser, "<path d=\"m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 "
                      "1 1 2.5 0 3.4L13 21\"/>"
                      "<path d=\"M22 21H7\"/><path d=\"m5 11 9 9\"/>"},

    // 5. Excerpt Crop Tool
    {AppIcon::Crop, "<path d=\"M6 2v14a2 2 0 0 0 2 2h14\"/><path d=\"M18 22V8a2 2 0 0 0-2-2H2\"/>"},

    // 6. Connector / Link Tool
    {AppIcon::Link, "<path d=\"M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71\"/>"
                    "<path d=\"M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71\"/>"},

    // 7. Undo
    {AppIcon::Undo,
     "<path d=\"M3 7v6h6\"/><path d=\"M21 17a9 9 0 0 0-9-9 9 9 0 0 0-6 2.3L3 13\"/>"},

    // 8. Redo
    {AppIcon::Redo,
     "<path d=\"M21 7v6h-6\"/><path d=\"M3 17a9 9 0 0 1 9-9 9 9 0 0 1 6 2.3l3 2.7\"/>"},

    // 9. Zoom In
    {AppIcon::ZoomIn,
     "<circle cx=\"11\" cy=\"11\" r=\"8\"/><line x1=\"21\" y1=\"21\" x2=\"16.65\" y2=\"16.65\"/>"
     "<line x1=\"11\" y1=\"8\" x2=\"11\" y2=\"14\"/><line x1=\"8\" y1=\"11\" x2=\"14\" "
     "y2=\"11\"/>"},

    // 10. Zoom Out
    {AppIcon::ZoomOut,
     "<circle cx=\"11\" cy=\"11\" r=\"8\"/><line x1=\"21\" y1=\"21\" x2=\"16.65\" y2=\"16.65\"/>"
     "<line x1=\"8\" y1=\"11\" x2=\"14\" y2=\"11\"/>"},

    // 11. Reset View / Fit
    {AppIcon::ResetView,
     "<path d=\"M3 7V5a2 2 0 0 1 2-2h2\"/><path d=\"M17 3h2a2 2 0 0 1 2 2v2\"/>"
     "<path d=\"M21 17v2a2 2 0 0 1-2 2h-2\"/><path d=\"M7 21H5a2 2 0 0 1-2-2v-2\"/>"
     "<circle cx=\"12\" cy=\"12\" r=\"3\"/>"},

    // 12. Minimap
    {AppIcon::Minimap,
     "<polygon points=\"3 6 9 3 15 6 21 3 21 18 15 21 9 18 3 21\"/>"
     "<line x1=\"9\" y1=\"3\" x2=\"9\" y2=\"18\"/><line x1=\"15\" y1=\"6\" x2=\"15\" y2=\"21\"/>"},

    // 13. Search
    {AppIcon::Search, "<circle cx=\"11\" cy=\"11\" r=\"8\"/><path d=\"m21 21-4.3-4.3\"/>"},

    // 14. Export
    {AppIcon::Export,
     "<path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><polyline points=\"17 8 12 3 7 8\"/>"
     "<line x1=\"12\" y1=\"3\" x2=\"12\" y2=\"15\"/>"},

    // 15. New File
    {AppIcon::NewFile, "<path d=\"M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 "
                       "2-2V7Z\"/><path d=\"M14 2v4a2 2 0 0 0 2 2h4\"/>"
                       "<path d=\"M12 18v-6\"/><path d=\"M9 15h6\"/>"},

    // 16. Open File
    {AppIcon::OpenFile, "<path d=\"m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 "
                        "2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 "
                        "1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2\"/>"},

    // 17. Save File
    {AppIcon::SaveFile,
     "<path d=\"M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 "
     "1-2-2V5a2 2 0 0 1 2-2z\"/>"
     "<path d=\"M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7\"/><path d=\"M7 3v4a1 1 0 0 0 1 1h7\"/>"},
};

const char* getColorHex(IconState state) {
    switch (state) {
    case IconState::Default:
        return "#334155"; // Slate 700
    case IconState::Active:
        return "#ffffff"; // Pure White
    case IconState::Disabled:
        return "#94a3b8"; // Slate 400
    }
    return "#334155";
}

const char* findInnerSvg(AppIcon icon) {
    for (const auto& def : kIconDefinitions) {
        if (def.id == icon) {
            return def.innerSvg;
        }
    }
    return "";
}

// Key for caching rendered pixbufs: (icon, size, state)
using PixbufCacheKey = std::tuple<AppIcon, int, IconState>;
static std::map<PixbufCacheKey, GdkPixbuf*> s_pixbufCache;

GdkPixbuf* renderSvgToPixbuf(const std::string& svgData, int size) {
    GInputStream* stream = g_memory_input_stream_new_from_data(
        svgData.data(), static_cast<gssize>(svgData.size()), nullptr);
    if (!stream) {
        return nullptr;
    }

    GError* err = nullptr;
    GdkPixbuf* pixbuf =
        gdk_pixbuf_new_from_stream_at_scale(stream, size, size, TRUE, nullptr, &err);
    g_object_unref(stream);

    if (!pixbuf) {
        if (err) {
            g_warning("[AppIcons] Failed to render SVG pixbuf (%dx%d): %s", size, size,
                      err->message);
            g_error_free(err);
        }
        return nullptr;
    }
    return pixbuf;
}

} // namespace

namespace AppIcons {

GdkPixbuf* getCachedPixbuf(AppIcon icon, int size, IconState state) {
    PixbufCacheKey key{icon, size, state};
    auto it = s_pixbufCache.find(key);
    if (it != s_pixbufCache.end()) {
        return it->second;
    }

    const char* inner = findInnerSvg(icon);
    if (!inner || *inner == '\0') {
        return nullptr;
    }

    const char* color = getColorHex(state);
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                      "width=\"24\" height=\"24\" fill=\"none\" stroke=\"";
    svg += color;
    svg += "\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">";
    svg += inner;
    svg += "</svg>";

    GdkPixbuf* pixbuf = renderSvgToPixbuf(svg, size);
    if (pixbuf) {
        s_pixbufCache[key] = pixbuf;
    }
    return pixbuf;
}

GtkWidget* createIconWidget(AppIcon icon, int size, IconState state) {
    GdkPixbuf* pixbuf = getCachedPixbuf(icon, size, state);
    if (pixbuf) {
        return gtk_image_new_from_pixbuf(pixbuf);
    }
    return gtk_image_new();
}

void setIconState(GtkWidget* imageWidget, AppIcon icon, IconState state, int size) {
    if (!imageWidget || !GTK_IS_IMAGE(imageWidget)) {
        return;
    }
    GdkPixbuf* pixbuf = getCachedPixbuf(icon, size, state);
    if (pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(imageWidget), pixbuf);
    }
}

} // namespace AppIcons

} // namespace FluidCoreApp
