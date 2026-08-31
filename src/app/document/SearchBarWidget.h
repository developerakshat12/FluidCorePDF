#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace FluidCoreApp {

class SearchBarWidget {
  public:
    SearchBarWidget();
    ~SearchBarWidget();

    SearchBarWidget(const SearchBarWidget&) = delete;
    SearchBarWidget& operator=(const SearchBarWidget&) = delete;

    GtkWidget* widget() const { return m_container; }

    void show(bool enableSqueeze = true);
    void hide();
    bool isVisible() const;
    void grabFocus();

    void setMatchStatus(std::size_t activeIndex, std::size_t totalMatches);

    void setQueryChangedCallback(std::function<void(const std::string& query, bool squeeze)> cb) {
        m_onQueryChanged = std::move(cb);
    }
    void setNavigateCallback(std::function<void(int direction)> cb) {
        m_onNavigate = std::move(cb);
    }
    void setSqueezeToggleCallback(std::function<void(bool squeeze)> cb) {
        m_onSqueezeToggled = std::move(cb);
    }
    void setCloseCallback(std::function<void()> cb) { m_onClose = std::move(cb); }

    std::string currentQuery() const;
    bool isSqueezeEnabled() const;

  private:
    static void onEntryChanged(GtkSearchEntry* entry, gpointer userData);
    static gboolean onEntryKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer userData);
    static void onPrevClicked(GtkButton* btn, gpointer userData);
    static void onNextClicked(GtkButton* btn, gpointer userData);
    static void onSqueezeSwitchToggled(GtkSwitch* sw, GParamSpec*, gpointer userData);
    static void onCloseClicked(GtkButton* btn, gpointer userData);

    GtkWidget* m_container = nullptr;
    GtkWidget* m_entry = nullptr;
    GtkWidget* m_prevBtn = nullptr;
    GtkWidget* m_nextBtn = nullptr;
    GtkWidget* m_countLabel = nullptr;
    GtkWidget* m_squeezeSwitch = nullptr;
    GtkWidget* m_closeBtn = nullptr;

    guint m_debounceTimerId = 0;

    std::function<void(const std::string&, bool)> m_onQueryChanged;
    std::function<void(int)> m_onNavigate;
    std::function<void(bool)> m_onSqueezeToggled;
    std::function<void()> m_onClose;
};

} // namespace FluidCoreApp
