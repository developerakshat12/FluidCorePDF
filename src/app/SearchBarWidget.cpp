#include "SearchBarWidget.h"

#include <iostream>

namespace FluidCoreApp {

SearchBarWidget::SearchBarWidget() {
    m_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(m_container, 8);
    gtk_widget_set_margin_end(m_container, 8);
    gtk_widget_set_margin_top(m_container, 8);
    gtk_widget_set_margin_bottom(m_container, 8);
    gtk_widget_set_halign(m_container, GTK_ALIGN_END);
    gtk_widget_set_valign(m_container, GTK_ALIGN_START);

    // Apply custom styling for a sleek floating search pill
    GtkStyleContext* context = gtk_widget_get_style_context(m_container);
    gtk_style_context_add_class(context, "osd");
    gtk_style_context_add_class(context, "toolbar");

    m_entry = gtk_search_entry_new();
    gtk_widget_set_size_request(m_entry, 220, -1);
    gtk_box_pack_start(GTK_BOX(m_container), m_entry, FALSE, FALSE, 0);

    m_prevBtn = gtk_button_new_with_label("▲");
    gtk_widget_set_tooltip_text(m_prevBtn, "Previous match (Shift+Enter / Shift+F3)");
    gtk_box_pack_start(GTK_BOX(m_container), m_prevBtn, FALSE, FALSE, 0);

    m_nextBtn = gtk_button_new_with_label("▼");
    gtk_widget_set_tooltip_text(m_nextBtn, "Next match (Enter / F3)");
    gtk_box_pack_start(GTK_BOX(m_container), m_nextBtn, FALSE, FALSE, 0);

    m_countLabel = gtk_label_new("0 of 0");
    gtk_widget_set_margin_start(m_countLabel, 4);
    gtk_widget_set_margin_end(m_countLabel, 4);
    gtk_box_pack_start(GTK_BOX(m_container), m_countLabel, FALSE, FALSE, 0);

    GtkWidget* sqLabel = gtk_label_new("Squeeze:");
    gtk_widget_set_margin_start(sqLabel, 6);
    gtk_box_pack_start(GTK_BOX(m_container), sqLabel, FALSE, FALSE, 0);

    m_squeezeSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(m_squeezeSwitch), TRUE);
    gtk_widget_set_tooltip_text(m_squeezeSwitch, "Accordion fold non-matching document regions");
    gtk_box_pack_start(GTK_BOX(m_container), m_squeezeSwitch, FALSE, FALSE, 0);

    m_closeBtn = gtk_button_new_with_label("✕");
    gtk_widget_set_tooltip_text(m_closeBtn, "Close search (Escape)");
    gtk_box_pack_start(GTK_BOX(m_container), m_closeBtn, FALSE, FALSE, 0);

    g_signal_connect(m_entry, "search-changed", G_CALLBACK(SearchBarWidget::onEntryChanged), this);
    g_signal_connect(m_entry, "key-press-event", G_CALLBACK(SearchBarWidget::onEntryKeyPress),
                     this);
    g_signal_connect(m_prevBtn, "clicked", G_CALLBACK(SearchBarWidget::onPrevClicked), this);
    g_signal_connect(m_nextBtn, "clicked", G_CALLBACK(SearchBarWidget::onNextClicked), this);
    g_signal_connect(m_squeezeSwitch, "notify::active",
                     G_CALLBACK(SearchBarWidget::onSqueezeSwitchToggled), this);
    g_signal_connect(m_closeBtn, "clicked", G_CALLBACK(SearchBarWidget::onCloseClicked), this);

    gtk_widget_set_no_show_all(m_container, TRUE);
    gtk_widget_hide(m_container);
}

SearchBarWidget::~SearchBarWidget() {
    if (m_debounceTimerId != 0) {
        g_source_remove(m_debounceTimerId);
        m_debounceTimerId = 0;
    }
}

void SearchBarWidget::show(bool enableSqueeze) {
    gtk_switch_set_active(GTK_SWITCH(m_squeezeSwitch), enableSqueeze);
    gtk_widget_show_all(m_container);
    grabFocus();
}

void SearchBarWidget::hide() {
    if (m_debounceTimerId != 0) {
        g_source_remove(m_debounceTimerId);
        m_debounceTimerId = 0;
    }
    gtk_widget_hide(m_container);
}

bool SearchBarWidget::isVisible() const {
    return gtk_widget_get_visible(m_container);
}

void SearchBarWidget::grabFocus() {
    gtk_widget_grab_focus(m_entry);
}

std::string SearchBarWidget::currentQuery() const {
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(m_entry));
    return text ? std::string(text) : "";
}

bool SearchBarWidget::isSqueezeEnabled() const {
    return gtk_switch_get_active(GTK_SWITCH(m_squeezeSwitch));
}

void SearchBarWidget::setMatchStatus(std::size_t activeIndex, std::size_t totalMatches) {
    if (totalMatches == 0) {
        gtk_label_set_text(GTK_LABEL(m_countLabel), "No matches");
    } else {
        std::string status =
            std::to_string(activeIndex + 1) + " of " + std::to_string(totalMatches);
        gtk_label_set_text(GTK_LABEL(m_countLabel), status.c_str());
    }
}

void SearchBarWidget::onEntryChanged(GtkSearchEntry*, gpointer userData) {
    auto* self = static_cast<SearchBarWidget*>(userData);
    if (self->m_debounceTimerId != 0) {
        g_source_remove(self->m_debounceTimerId);
    }

    self->m_debounceTimerId = g_timeout_add(
        150,
        +[](gpointer data) -> gboolean {
            auto* s = static_cast<SearchBarWidget*>(data);
            if (s->m_onQueryChanged) {
                s->m_onQueryChanged(s->currentQuery(), s->isSqueezeEnabled());
            }
            s->m_debounceTimerId = 0;
            return G_SOURCE_REMOVE;
        },
        self);
}

gboolean SearchBarWidget::onEntryKeyPress(GtkWidget*, GdkEventKey* event, gpointer userData) {
    auto* self = static_cast<SearchBarWidget*>(userData);
    const bool shift = (event->state & GDK_SHIFT_MASK) != 0;

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        if (self->m_onNavigate) {
            self->m_onNavigate(shift ? -1 : 1);
        }
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F3) {
        if (self->m_onNavigate) {
            self->m_onNavigate(shift ? -1 : 1);
        }
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        self->hide();
        if (self->m_onClose) {
            self->m_onClose();
        }
        return TRUE;
    }
    return FALSE;
}

void SearchBarWidget::onPrevClicked(GtkButton*, gpointer userData) {
    auto* self = static_cast<SearchBarWidget*>(userData);
    if (self->m_onNavigate) {
        self->m_onNavigate(-1);
    }
}

void SearchBarWidget::onNextClicked(GtkButton*, gpointer userData) {
    auto* self = static_cast<SearchBarWidget*>(userData);
    if (self->m_onNavigate) {
        self->m_onNavigate(1);
    }
}

void SearchBarWidget::onSqueezeSwitchToggled(GtkSwitch*, GParamSpec*, gpointer userData) {
    auto* self = static_cast<SearchBarWidget*>(userData);
    if (self->m_onSqueezeToggled) {
        self->m_onSqueezeToggled(self->isSqueezeEnabled());
    }
}

void SearchBarWidget::onCloseClicked(GtkButton*, gpointer userData) {
    auto* self = static_cast<SearchBarWidget*>(userData);
    self->hide();
    if (self->m_onClose) {
        self->m_onClose();
    }
}

} // namespace FluidCoreApp
