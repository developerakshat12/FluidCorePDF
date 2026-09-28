#pragma once

#include <gdk/gdk.h>

#include <cstdint>
#include <string>

namespace FluidCoreApp {

// Maps GDK stylus button and device events onto ink intents.
//
// GDK's Win32 backend already surfaces Wintab hardware as ordinary GDK input: a pen
// arrives as GDK_SOURCE_PEN with a GDK_AXIS_PRESSURE axis, the eraser end as
// GDK_SOURCE_ERASER, and barrel buttons as plain GdkEventButton presses on that same
// pen device using button 2 or button 3. There is no Windows Ink / WinRT InkPresenter
// integration, and none is needed for these behaviors; GDK is the abstraction layer the
// rest of the app is built on.
//
// Left alone, a barrel press looks identical to a middle click, so it triggers canvas
// panning or a context menu instead of erasing. This router lets the ink surfaces claim
// barrel presses while leaving genuine mouse clicks untouched.
class StylusButtonRouter {
  public:
    enum class Intent {
        Ignore,     // Not a press this router owns.
        Inking,     // Primary press with a drawing tool.
        Erasing,    // Eraser device end, or a pen barrel button.
        Panning,    // Genuine middle click; leave the view's pan handling alone.
        ContextMenu // Genuine secondary click.
    };

    // True for the pen and eraser ends of a stylus.
    static bool isStylusSource(GdkInputSource source);

    // True only for the pen end. The eraser end is a separate GdkInputSource in GDK3
    // and does not deliver barrel buttons.
    static bool isPenSource(GdkInputSource source);

    // Classifies a button press. Barrel buttons on a stylus device become Erasing;
    // mouse presses keep the view's existing meaning.
    static Intent classifyPress(guint button, GdkInputSource source);

    // True when the press is a barrel button, i.e. a non-primary button delivered by
    // the pen end of a stylus. Used to hold the eraser for the duration of the press.
    static bool isBarrelButton(guint button, GdkInputSource source);

    // Resolves which tool a press should activate given the currently selected tool.
    //
    // "select", "text", "crop", and "rect_select" are mouse-oriented modes: they start a
    // selection and consume the press. A stylus overrides them, otherwise touching the
    // page with a pen while the select tool was active selects text instead of drawing,
    // which forces the user to switch tools with a mouse before every annotation.
    //
    // Every other tool is honoured as selected, including the eraser. An earlier version
    // overrode unconditionally, which meant a stylus press with the eraser selected drew
    // a stroke at the eraser's forced width instead of erasing.
    static std::string resolveInkTool(const std::string& activeTool, GdkInputSource source,
                                      bool highlighterModifier);

    // --- Barrel-button state machine ---
    //
    // Tracks the eraser bind across a barrel press/release pair so the previously
    // selected tool can be restored, and supports latching the eraser on a long press
    // for users who would rather not hold the button.

    // Records a barrel press and returns the tool that was active beforehand, so the
    // caller can restore it on release. `latchRequest` is set when the press has been
    // held past the latch threshold, which pins the eraser until the next press.
    std::string beginBarrel(const std::string& activeTool);

    // Releases a barrel press. Returns true when the previously active tool should be
    // restored, and false when a latched eraser stays bound.
    bool endBarrel();

    // Pins the eraser until the next press, for users who would rather not hold the
    // barrel button down. Called once the press has exceeded kBarrelLatchMs.
    void latchBarrel();

    // True when the pen barrel press borrows the eraser.
    bool barrelActive() const { return m_barrelActive; }
    bool barrelLatched() const { return m_barrelLatched; }

    // Whether a borrowed-tool restore should be applied.
    //
    // A borrow is only unwound when the tool is still the one the borrow installed. The
    // user can change tools while the pen or barrel button is still down, and the
    // toolbar writes straight to the tool selection, bypassing whatever was saved at
    // borrow time. Restoring unconditionally then overwrites that newer choice with a
    // stale value, which silently reverted the eraser to the pen and made it look like
    // the tool change had not taken effect.
    static bool shouldRestoreBorrowedTool(const std::string& toolBeforeBorrow,
                                          const std::string& toolInstalledByBorrow,
                                          const std::string& currentTool) {
        if (toolBeforeBorrow.empty()) {
            return false;
        }
        return currentTool == toolInstalledByBorrow;
    }

    // Milliseconds a barrel press must be held before the eraser latches. Exposed so the
    // UI can be tested against it rather than a magic number.
    static constexpr uint32_t kBarrelLatchMs = 400;

    // --- Stylus attributes ---
    //
    // Reads tilt, hover distance, and barrel rotation from a GDK event. Not every
    // Wintab digitizer exposes all of them and GDK3 offers no way to enumerate a
    // device's axes, so availability is probed on the first event from a device and
    // cached rather than assumed. That keeps a pen without a tilt sensor from
    // reporting a constant zero, which is indistinguishable from a pen held
    // perfectly upright.
    struct Attributes {
        double tiltX = 0.0;
        double tiltY = 0.0;
        double distance = 0.0; // hover height above the surface
        double rotation = 0.0; // barrel rotation, radians
        bool hasTilt = false;
        bool hasDistance = false;
        bool hasRotation = false;

        bool any() const { return hasTilt || hasDistance || hasRotation; }
    };

    Attributes readAttributes(GdkEvent* event, GdkDevice* device);

  private:
    std::string m_toolBeforeBarrel;
    bool m_barrelActive = false;
    bool m_barrelLatched = false;

    GdkDevice* m_attrDevice = nullptr;
    bool m_attrProbed = false;
    bool m_hasTilt = false;
    bool m_hasDistance = false;
    bool m_hasRotation = false;
};

} // namespace FluidCoreApp
