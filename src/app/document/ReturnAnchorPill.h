#pragma once

#include "FluidCoreAPI.h"

#include <cairo.h>
#include <gtk/gtk.h>

#include <functional>
#include <string>

namespace FluidCoreApp {

// Pure geometry helper for hit-testing and layout calculations of the ReturnAnchorPill.
// Tested headlessly without requiring a live GTK display server.
struct ReturnAnchorPillGeometry {
    static constexpr double kDefaultWidth = 260.0;
    static constexpr double kDefaultHeight = 36.0;
    static constexpr double kCloseButtonWidth = 28.0;
    static constexpr double kPadding = 8.0;

    static bool isInsidePill(double localX, double localY, double width = kDefaultWidth,
                             double height = kDefaultHeight) {
        return localX >= 0.0 && localX <= width && localY >= 0.0 && localY <= height;
    }

    static bool isInsideCloseButton(double localX, double localY, double width = kDefaultWidth,
                                    double height = kDefaultHeight) {
        if (!isInsidePill(localX, localY, width, height)) {
            return false;
        }
        const double closeX = width - kCloseButtonWidth - kPadding * 0.5;
        return localX >= closeX && localX <= width;
    }

    static bool isInsideReturnAction(double localX, double localY, double width = kDefaultWidth,
                                     double height = kDefaultHeight) {
        if (!isInsidePill(localX, localY, width, height)) {
            return false;
        }
        return !isInsideCloseButton(localX, localY, width, height);
    }
};

// Interactive floating overlay pill component in DocumentPane (specs/integration.md §3, TRD §3.5).
// When navigated from a workspace excerpt card, displays a glassmorphic pill allowing instant
// return navigation to the exact origin world coordinates on the infinite canvas.
class ReturnAnchorPill {
  public:
    using ReturnCallback =
        std::function<void(const FluidCore::Point& originWorldCoord, const std::string& excerptId)>;
    using DismissCallback = std::function<void()>;

    ReturnAnchorPill();
    ~ReturnAnchorPill();

    ReturnAnchorPill(const ReturnAnchorPill&) = delete;
    ReturnAnchorPill& operator=(const ReturnAnchorPill&) = delete;

    GtkWidget* widget() const { return m_area; }

    void show(const std::string& excerptId, const std::string& snippet,
              const FluidCore::Point& originWorldCoord);
    void hide();
    bool isVisible() const { return m_visible; }

    const std::string& excerptId() const { return m_excerptId; }
    const std::string& snippet() const { return m_snippet; }
    const FluidCore::Point& originWorldCoord() const { return m_originWorldCoord; }

    void setOnReturnClicked(ReturnCallback cb) { m_onReturnClicked = std::move(cb); }
    void setOnDismissClicked(DismissCallback cb) { m_onDismissClicked = std::move(cb); }

  private:
    static void drawCallback(GtkWidget* widget, cairo_t* cr, gpointer userData);
    static gboolean buttonPressCallback(GtkWidget* widget, GdkEventButton* event,
                                        gpointer userData);
    static gboolean motionNotifyCallback(GtkWidget* widget, GdkEventMotion* event,
                                         gpointer userData);
    static gboolean leaveNotifyCallback(GtkWidget* widget, GdkEventCrossing* event,
                                        gpointer userData);

    void draw(cairo_t* cr);
    gboolean onButtonPress(GdkEventButton* event);
    gboolean onMotionNotify(GdkEventMotion* event);
    gboolean onLeaveNotify(GdkEventCrossing* event);

    GtkWidget* m_area = nullptr;

    bool m_visible = false;
    std::string m_excerptId;
    std::string m_snippet;
    FluidCore::Point m_originWorldCoord{0.0, 0.0};

    bool m_hoverReturn = false;
    bool m_hoverClose = false;

    ReturnCallback m_onReturnClicked;
    DismissCallback m_onDismissClicked;
};

} // namespace FluidCoreApp
