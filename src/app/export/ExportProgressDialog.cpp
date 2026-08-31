#include "export/ExportProgressDialog.h"

#include <algorithm>
#include <iostream>

namespace FluidCoreApp {

ExportProgressDialog::ExportProgressDialog(GtkWindow* parent,
                                           const std::string& inputPdfPath,
                                           std::vector<FluidCore::Stroke> strokesSnapshot,
                                           const std::string& outputPath,
                                           const PdfExportOptions& options,
                                           CompletionCallback onFinished)
    : m_cancelFlag(std::make_shared<std::atomic<bool>>(false)),
      m_lifetimeToken(std::make_shared<int>(1)),
      m_onFinished(std::move(onFinished)) {

    m_dialog = gtk_dialog_new_with_buttons(
        "Exporting Document", parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, nullptr);

    gtk_window_set_default_size(GTK_WINDOW(m_dialog), 380, -1);
    gtk_window_set_resizable(GTK_WINDOW(m_dialog), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(m_dialog), FALSE); // Must use Cancel button

    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(m_dialog));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 16);
    gtk_box_set_spacing(GTK_BOX(contentArea), 10);

    GtkWidget* titleLabel = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(titleLabel), "<b><big>Exporting Flattened PDF...</big></b>");
    gtk_label_set_xalign(GTK_LABEL(titleLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(contentArea), titleLabel, FALSE, FALSE, 0);

    m_statusLabel = gtk_label_new("Preparing vector page pipeline...");
    gtk_label_set_xalign(GTK_LABEL(m_statusLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(contentArea), m_statusLabel, FALSE, FALSE, 0);

    m_progressBar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(m_progressBar), TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_progressBar), 0.0);
    gtk_box_pack_start(GTK_BOX(contentArea), m_progressBar, FALSE, FALSE, 0);

    g_signal_connect(m_dialog, "response",
                     G_CALLBACK(+[](GtkDialog* dialog, gint responseId, gpointer data) {
                         auto* self = static_cast<ExportProgressDialog*>(data);
                         if (self && responseId == GTK_RESPONSE_CANCEL && self->m_cancelFlag) {
                             self->m_cancelFlag->store(true);
                             gtk_label_set_text(GTK_LABEL(self->m_statusLabel),
                                                "Cancelling export (cleaning temporary files)...");
                             GtkWidget* btn = gtk_dialog_get_widget_for_response(dialog, GTK_RESPONSE_CANCEL);
                             if (btn) {
                                 gtk_button_set_label(GTK_BUTTON(btn), "Cancelling...");
                                 gtk_widget_set_sensitive(btn, FALSE);
                             }
                         }
                     }),
                     this);

    gtk_widget_show_all(m_dialog);

    std::weak_ptr<void> tokenWeak = m_lifetimeToken;

    // Launch worker thread
    m_workerThread = PdfExportService::exportAnnotatedPdfAsync(
        inputPdfPath, std::move(strokesSnapshot), outputPath, options, m_cancelFlag, tokenWeak,
        [this](std::size_t curr, std::size_t total) {
            updateProgress(curr, total);
        },
        [this](const PdfExportResult& res) {
            finish(res);
        });
}

ExportProgressDialog::~ExportProgressDialog() {
    // 1. Signal cancellation
    if (m_cancelFlag) {
        m_cancelFlag->store(true);
    }
    // 2. Join worker thread cleanly
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    // 3. Invalidate lifetime token so queued in-flight idle events return immediately
    m_lifetimeToken.reset();
    // 4. Destroy GTK dialog widgets
    if (m_dialog) {
        gtk_widget_destroy(m_dialog);
        m_dialog = nullptr;
    }
}

void ExportProgressDialog::updateProgress(std::size_t current, std::size_t total) {
    if (!m_dialog || !m_progressBar || !m_statusLabel || total == 0) {
        return;
    }
    const double fraction = std::clamp(static_cast<double>(current) / static_cast<double>(total), 0.0, 1.0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_progressBar), fraction);

    const int pct = static_cast<int>(fraction * 100.0);
    std::string text = "Page " + std::to_string(current) + " of " + std::to_string(total) + " (" +
                       std::to_string(pct) + "%)";
    gtk_label_set_text(GTK_LABEL(m_statusLabel), text.c_str());
}

void ExportProgressDialog::finish(const PdfExportResult& result) {
    if (m_finished) return;
    m_finished = true;

    if (m_onFinished) {
        m_onFinished(result);
    }

    // Auto-destroy dialog upon finish
    delete this;
}

} // namespace FluidCoreApp
