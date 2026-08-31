#include "export/ExportDialog.h"
#include "export/ExportProgressDialog.h"
#include "services/PdfExportService.h"

#include <atomic>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace FluidCoreApp {

namespace {

std::string getBaseDocumentName(const std::string& path) {
    if (path.empty()) {
        return "Synthesis";
    }
    std::filesystem::path p(path);
    std::string stem = p.stem().string();
    return stem.empty() ? "Synthesis" : stem;
}

void showMessage(GtkWindow* parent, GtkMessageType type, const std::string& title,
                 const std::string& message) {
    GtkWidget* dialog =
        gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", title.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static std::atomic<bool> s_isExporting{false};

} // namespace

void ExportDialog::show(GtkWindow* parent, DocumentPane* pane, WorkspaceView* /*workspace*/,
                        FluidCore::FluidCoreAPI* api) {
    if (s_isExporting.load()) {
        showMessage(
            parent, GTK_MESSAGE_WARNING, "Export In Progress",
            "An export is already running in the background. Please wait for it to finish.");
        return;
    }

    const std::string baseName = pane ? getBaseDocumentName(pane->pdfPath()) : "Synthesis";

    // Pre-scan annotated pages count
    std::set<std::size_t> annotatedPages;
    if (pane) {
        for (const auto& stroke : pane->annotationStore().strokes()) {
            annotatedPages.insert(stroke.pageIndex);
        }
    }
    const std::size_t numAnnotated = annotatedPages.size();
    const std::size_t totalDocPages = pane ? pane->pages().size() : 0;

    GtkWidget* chooser = gtk_file_chooser_dialog_new(
        "Export Synthesis & Annotations", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_window_set_modal(GTK_WINDOW(chooser), TRUE);

    // File format filters
    GtkFileFilter* pdfFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(pdfFilter, "Annotated PDF (*.pdf)");
    gtk_file_filter_add_pattern(pdfFilter, "*.pdf");
    gtk_file_filter_add_mime_type(pdfFilter, "application/pdf");

    GtkFileFilter* mdFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(mdFilter, "Workspace Markdown Outline (*.md)");
    gtk_file_filter_add_pattern(mdFilter, "*.md");
    gtk_file_filter_add_mime_type(mdFilter, "text/markdown");

    GtkFileFilter* allFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(allFilter, "All Supported Formats (*.pdf, *.md)");
    gtk_file_filter_add_pattern(allFilter, "*.pdf");
    gtk_file_filter_add_pattern(allFilter, "*.md");

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), pdfFilter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), mdFilter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), allFilter);

    // Extra options panel
    GtkWidget* extraBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(extraBox), 10);

    GtkWidget* formatLabel = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(formatLabel), "<b>Export Target:</b>");
    gtk_label_set_xalign(GTK_LABEL(formatLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(extraBox), formatLabel, FALSE, FALSE, 0);

    std::string fullDocLabel = "📄 Flattened Annotated PDF (Full Document";
    if (totalDocPages > 0)
        fullDocLabel += " — " + std::to_string(totalDocPages) + " Pages";
    fullDocLabel += ")";

    std::string compactLabel =
        "⚡ Annotated Pages Only — Compact Synthesis (" + std::to_string(numAnnotated) + " Pages)";

    GtkWidget* pdfFullRadio = gtk_radio_button_new_with_label(nullptr, fullDocLabel.c_str());
    GtkWidget* pdfCompactRadio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(pdfFullRadio), compactLabel.c_str());
    GtkWidget* mdRadio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(pdfFullRadio), "📝 Workspace Markdown Outline (*.md)");

    gtk_box_pack_start(GTK_BOX(extraBox), pdfFullRadio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(extraBox), pdfCompactRadio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(extraBox), mdRadio, FALSE, FALSE, 0);

    // Disable compact radio if 0 pages are annotated
    if (numAnnotated == 0) {
        gtk_widget_set_sensitive(pdfCompactRadio, FALSE);
    }

    // Options section
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(extraBox), sep, FALSE, FALSE, 4);

    GtkWidget* mermaidCheck =
        gtk_check_button_new_with_label("Include Mermaid relationship diagram (for Markdown)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mermaidCheck), TRUE);
    gtk_box_pack_start(GTK_BOX(extraBox), mermaidCheck, FALSE, FALSE, 0);

    GtkWidget* citationsCheck =
        gtk_check_button_new_with_label("Include source citations & tag references");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(citationsCheck), TRUE);
    gtk_box_pack_start(GTK_BOX(extraBox), citationsCheck, FALSE, FALSE, 0);

    gtk_widget_show_all(extraBox);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(chooser), extraBox);

    // Set initial default name
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser),
                                      (baseName + "-annotated.pdf").c_str());

    // Connect radio button toggles to update current proposed name & filter
    struct ToggleContext {
        GtkWidget* chooser;
        std::string base;
        GtkFileFilter* pdfFilter;
        GtkFileFilter* mdFilter;
        GtkWidget* pdfFullRadio;
        GtkWidget* pdfCompactRadio;
    };
    auto* ctx =
        new ToggleContext{chooser, baseName, pdfFilter, mdFilter, pdfFullRadio, pdfCompactRadio};

    auto updateProposedName = +[](GtkToggleButton*, gpointer data) {
        auto* c = static_cast<ToggleContext*>(data);
        const bool isFull = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c->pdfFullRadio));
        const bool isCompact = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c->pdfCompactRadio));

        if (isFull) {
            gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(c->chooser), c->pdfFilter);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c->chooser),
                                              (c->base + "-annotated.pdf").c_str());
        } else if (isCompact) {
            gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(c->chooser), c->pdfFilter);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c->chooser),
                                              (c->base + "-compact-annotated.pdf").c_str());
        } else {
            gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(c->chooser), c->mdFilter);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c->chooser),
                                              (c->base + "-workspace-outline.md").c_str());
        }
    };

    g_signal_connect(pdfFullRadio, "toggled", G_CALLBACK(updateProposedName), ctx);
    g_signal_connect(pdfCompactRadio, "toggled", G_CALLBACK(updateProposedName), ctx);
    g_signal_connect_data(
        mdRadio, "toggled", G_CALLBACK(updateProposedName), ctx,
        [](gpointer data, GClosure*) { delete static_cast<ToggleContext*>(data); },
        static_cast<GConnectFlags>(0));

    const gint response = gtk_dialog_run(GTK_DIALOG(chooser));
    if (response == GTK_RESPONSE_ACCEPT) {
        gchar* rawPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (rawPath) {
            std::string targetPath(rawPath);
            g_free(rawPath);

            const bool isFullPdf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pdfFullRadio));
            const bool isCompactPdf =
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pdfCompactRadio));
            const bool isPdf = (isFullPdf || isCompactPdf);

            if (isPdf) {
                // Enforce .pdf extension
                if (targetPath.size() < 4 || targetPath.substr(targetPath.size() - 4) != ".pdf") {
                    targetPath += ".pdf";
                }

                if (!pane || pane->pdfPath().empty()) {
                    showMessage(parent, GTK_MESSAGE_ERROR, "PDF Export Failed",
                                "No valid source PDF file path available.");
                } else {
                    PdfExportOptions options;
                    options.onlyAnnotatedPages = isCompactPdf;
                    options.includeStrokes = true;
                    options.includeHighlighters = true;

                    s_isExporting.store(true);

                    // Copy snapshot of strokes by value
                    std::vector<FluidCore::Stroke> snapshot = pane->annotationStore().strokes();
                    const std::string inputPdfPath = pane->pdfPath();

                    // Launch async progress dialog
                    new ExportProgressDialog(
                        parent, inputPdfPath, std::move(snapshot), targetPath, options,
                        [parent, targetPath, isCompactPdf](const PdfExportResult& res) {
                            s_isExporting.store(false);
                            if (res.success) {
                                std::string msg =
                                    "Annotated PDF with vector strokes was saved to:\n" +
                                    targetPath + "\n(" + std::to_string(res.pagesExported) +
                                    " pages exported)";
                                if (isCompactPdf) {
                                    msg +=
                                        "\n\nNote: Compact mode exports sequential subset pages. "
                                        "Source citations retain original document pagination.";
                                }
                                showMessage(parent, GTK_MESSAGE_INFO, "Export Successful", msg);
                            } else {
                                showMessage(parent, GTK_MESSAGE_ERROR, "PDF Export Failed",
                                            res.errorMessage.empty()
                                                ? "Export failed or was cancelled."
                                                : res.errorMessage);
                            }
                        });
                }
            } else {
                // Enforce .md extension
                if (targetPath.size() < 3 || targetPath.substr(targetPath.size() - 3) != ".md") {
                    targetPath += ".md";
                }

                if (!api) {
                    showMessage(parent, GTK_MESSAGE_ERROR, "Markdown Export Failed",
                                "Workspace API handle is null.");
                } else {
                    FluidCore::WorkspaceExportOptions opts;
                    opts.customTitle = "Synthesis: " + baseName;
                    opts.includeMermaidGraph =
                        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mermaidCheck));
                    opts.includeSourceCitations =
                        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(citationsCheck));

                    std::string err;
                    bool ok = api->exportWorkspaceMarkdownToFile(targetPath, opts);
                    if (ok) {
                        showMessage(parent, GTK_MESSAGE_INFO, "Export Successful",
                                    "Workspace Markdown outline was exported to:\n" + targetPath);
                    } else {
                        showMessage(parent, GTK_MESSAGE_ERROR, "Markdown Export Failed",
                                    "Could not write Markdown outline to " + targetPath);
                    }
                }
            }
        }
    }

    gtk_widget_destroy(chooser);
}

} // namespace FluidCoreApp
