#pragma once

#include "services/PdfExportService.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtk/gtk.h>

namespace FluidCoreApp {

// RAII modal progress dialog owning the joinable export worker thread.
// Guarantees strict ordered destruction: cancel -> join -> invalidate token -> destroy widget.
class ExportProgressDialog {
  public:
    ExportProgressDialog(GtkWindow* parent, const std::string& inputPdfPath,
                         std::vector<FluidCore::Stroke> strokesSnapshot,
                         const std::string& outputPath, const PdfExportOptions& options,
                         CompletionCallback onFinished);

    ~ExportProgressDialog();

    ExportProgressDialog(const ExportProgressDialog&) = delete;
    ExportProgressDialog& operator=(const ExportProgressDialog&) = delete;

    void updateProgress(std::size_t current, std::size_t total);
    void finish(const PdfExportResult& result);

  private:
    GtkWidget* m_dialog = nullptr;
    GtkWidget* m_progressBar = nullptr;
    GtkWidget* m_statusLabel = nullptr;
    GtkWidget* m_cancelBtn = nullptr;

    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    std::shared_ptr<int> m_lifetimeToken;
    std::thread m_workerThread;
    CompletionCallback m_onFinished;
    bool m_finished = false;
};

} // namespace FluidCoreApp
