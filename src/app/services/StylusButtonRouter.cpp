#include "services/StylusButtonRouter.h"

#include <cmath>

namespace FluidCoreApp {

bool StylusButtonRouter::isStylusSource(GdkInputSource source) {
    return source == GDK_SOURCE_PEN || source == GDK_SOURCE_ERASER;
}

bool StylusButtonRouter::isPenSource(GdkInputSource source) {
    return source == GDK_SOURCE_PEN;
}

bool StylusButtonRouter::isBarrelButton(guint button, GdkInputSource source) {
    // Only the pen end carries barrel buttons, and GDK reports them as the middle or
    // secondary button. Restricting to GDK_SOURCE_PEN is what keeps a real mouse's
    // middle click panning and its right click opening a context menu.
    if (!isPenSource(source)) {
        return false;
    }
    return button == GDK_BUTTON_MIDDLE || button == GDK_BUTTON_SECONDARY;
}

StylusButtonRouter::Intent StylusButtonRouter::classifyPress(guint button, GdkInputSource source) {
    if (isBarrelButton(button, source)) {
        return Intent::Erasing;
    }
    if (source == GDK_SOURCE_ERASER) {
        return Intent::Erasing;
    }
    switch (button) {
    case GDK_BUTTON_PRIMARY:
        return Intent::Inking;
    case GDK_BUTTON_MIDDLE:
        return Intent::Panning;
    case GDK_BUTTON_SECONDARY:
        return Intent::ContextMenu;
    default:
        return Intent::Ignore;
    }
}

std::string StylusButtonRouter::beginBarrel(const std::string& activeTool) {
    m_toolBeforeBarrel = activeTool;
    m_barrelActive = true;
    return activeTool;
}

bool StylusButtonRouter::endBarrel() {
    m_barrelActive = false;
    if (m_barrelLatched) {
        return false;
    }
    m_toolBeforeBarrel.clear();
    return true;
}

void StylusButtonRouter::latchBarrel() {
    m_barrelLatched = true;
}

StylusButtonRouter::Attributes StylusButtonRouter::readAttributes(GdkEvent* event,
                                                                  GdkDevice* device) {
    Attributes attrs;
    if (!event || !device || !isStylusSource(gdk_device_get_source(device))) {
        return attrs;
    }

    // Probe once per device. GDK3 offers no way to enumerate a device's axes, so the
    // only reliable availability check is whether reading the axis succeeds.
    if (!m_attrProbed || m_attrDevice != device) {
        m_attrDevice = device;
        m_attrProbed = true;
        gdouble probe = 0.0;
        m_hasTilt = gdk_event_get_axis(event, GDK_AXIS_XTILT, &probe) == TRUE;
        m_hasDistance = gdk_event_get_axis(event, GDK_AXIS_DISTANCE, &probe) == TRUE;
        m_hasRotation = gdk_event_get_axis(event, GDK_AXIS_ROTATION, &probe) == TRUE;
    }

    gdouble tiltX = 0.0;
    gdouble tiltY = 0.0;
    if (m_hasTilt && gdk_event_get_axis(event, GDK_AXIS_XTILT, &tiltX) == TRUE) {
        if (gdk_event_get_axis(event, GDK_AXIS_YTILT, &tiltY) != TRUE) {
            tiltY = 0.0;
        }
        if (std::isfinite(tiltX) && std::isfinite(tiltY)) {
            attrs.tiltX = tiltX;
            attrs.tiltY = tiltY;
            attrs.hasTilt = true;
        }
    }

    gdouble distance = 0.0;
    if (m_hasDistance && gdk_event_get_axis(event, GDK_AXIS_DISTANCE, &distance) == TRUE &&
        std::isfinite(distance)) {
        attrs.distance = distance;
        attrs.hasDistance = true;
    }

    gdouble rotation = 0.0;
    if (m_hasRotation && gdk_event_get_axis(event, GDK_AXIS_ROTATION, &rotation) == TRUE &&
        std::isfinite(rotation)) {
        attrs.rotation = rotation;
        attrs.hasRotation = true;
    }

    return attrs;
}

} // namespace FluidCoreApp
