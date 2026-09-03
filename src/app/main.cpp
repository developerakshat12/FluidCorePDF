#include "FluidCoreEngine.h"
#include "document/DocumentPane.h"
#include "export/ExportDialog.h"
#include "services/ExcerptTileCache.h"
#include "services/PdfDocumentService.h"
#include "services/ToolManager.h"
#include "window/AppHeaderBar.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/TopToolbarWidget.h"
#include "workspace/WorkspaceView.h"

#include <filesystem>
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using FluidCore::Color;
using FluidCore::ExcerptCardNode;
using FluidCore::FluidCoreAPI;
using FluidCore::FluidCoreEngine;
using FluidCore::Rectangle;

std::string normalizePath(std::string path) {
    if (path.empty()) {
        return path;
    }
#ifndef G_OS_WIN32
    // If running on Linux/WSL and passed a Windows path like "D:\foo\bar.pdf"
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(path[0])));
        std::string sub = path.substr(2);
        for (char& c : sub) {
            if (c == '\\') {
                c = '/';
            }
        }
        return std::string("/mnt/") + drive + sub;
    }
#endif
    return path;
}

void showMessage(GtkWindow* parent, GtkMessageType type, const std::string& title,
                 const std::string& message) {
    GtkWidget* dialog =
        gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", title.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Minimal concrete node so the demo shell can seed generic notes alongside excerpts.
class SampleNode final : public FluidCore::WorkspaceNode {
  public:
    SampleNode(std::string id, Rectangle bounds) : m_id(std::move(id)), m_bounds(bounds) {}
    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return m_bounds; }
    void setPosition(double x, double y) override {
        m_bounds.x = x;
        m_bounds.y = y;
    }

  private:
    std::string m_id;
    Rectangle m_bounds;
};

void seedDemoContent(FluidCoreAPI& api, const std::string& docPath) {
    const std::string docRef = docPath.empty() ? "doc-primary.pdf" : docPath;

    // Cluster 1: Primary PDF excerpts (Drag-out Excerpt Cards)
    auto card1 = std::make_unique<ExcerptCardNode>(
        "excerpt-clause-1", Rectangle{80.0, 80.0, 260.0, 150.0}, docRef, 0,
        Rectangle{0.08, 0.12, 0.84, 0.18},
        "The infinite 2D canvas provides unconstrained spatial arrangement for research synthesis "
        "and literature clustering #synthesis #canvas.",
        false, Color{255, 220, 0, 255});
    card1->addTag("synthesis");
    card1->addTag("canvas");
    api.insertNode(std::move(card1));

    auto card2 = std::make_unique<ExcerptCardNode>(
        "excerpt-clause-2", Rectangle{370.0, 80.0, 260.0, 150.0}, docRef, 1,
        Rectangle{0.10, 0.20, 0.80, 0.22},
        "Spatial indexing with R*-tree enables O(log N) viewport culling and sub-millisecond query "
        "latencies across 100,000+ items #indexing #rtree.",
        false, Color{56, 189, 248, 255});
    card2->addTag("indexing");
    card2->addTag("rtree");
    api.insertNode(std::move(card2));

    api.insertNode(std::make_unique<ExcerptCardNode>(
        "excerpt-diagram-1", Rectangle{660.0, 80.0, 320.0, 208.0}, docRef, 0,
        Rectangle{0.08, 0.15, 0.84, 0.35}, "", true, Color{168, 85, 247, 255}));

    // Cluster 2: Synthesized notes
    api.insertNode(
        std::make_unique<SampleNode>("note-synthesis", Rectangle{180.0, 300.0, 260.0, 120.0}));
    api.insertNode(
        std::make_unique<SampleNode>("note-precedent", Rectangle{480.0, 300.0, 220.0, 110.0}));

    // Cluster 3: Distant comparative nodes across infinite canvas space
    api.insertNode(
        std::make_unique<SampleNode>("compare-patent-a", Rectangle{880.0, 480.0, 240.0, 150.0}));
    api.insertNode(
        std::make_unique<SampleNode>("compare-patent-b", Rectangle{1160.0, 480.0, 240.0, 150.0}));
    api.insertNode(
        std::make_unique<SampleNode>("summary-conclusion", Rectangle{540.0, 680.0, 300.0, 160.0}));
}

enum class ActivePane { Workspace, Document };

struct AppContext {
    FluidCoreEngine* engine = nullptr;
    const std::string* pdfPath = nullptr;
};

struct AppViewContext {
    FluidCoreApp::DocumentPane* pane = nullptr;
    FluidCoreApp::WorkspaceView* workspace = nullptr;
    FluidCoreApp::ToolManager* toolManager = nullptr;
    FluidCore::FluidCoreEngine* engine = nullptr;
    FluidCoreApp::PdfDocumentService* pdfDocService = nullptr;
    FluidCoreApp::ExcerptTileCache* excerptTileCache = nullptr;
    FluidCoreApp::AppHeaderBar* headerBar = nullptr;
    GtkWindow* window = nullptr;
    ActivePane* lastActivePane = nullptr;
    std::function<void()> updateUndoRedoUI;
    bool isProjectDirty = false;
    std::function<void()> pendingActionProceed = nullptr;
};

bool validateLtprojBundle(const std::string& path, std::string& errorMsg) {
    std::filesystem::path p(path);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) {
        errorMsg = "The selected path is not an existing directory.";
        return false;
    }
    if (!std::filesystem::exists(p / "project.db", ec)) {
        errorMsg = "The selected directory does not contain 'project.db'. It is not a valid "
                   "FluidCore project.";
        return false;
    }
    std::filesystem::path metaPath = p / "metadata.json";
    if (std::filesystem::exists(metaPath, ec)) {
        std::ifstream file(metaPath);
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            auto pos = content.find("\"schema_version\"");
            if (pos != std::string::npos) {
                auto colon = content.find(':', pos);
                if (colon != std::string::npos) {
                    auto numStart = content.find_first_of("0123456789", colon);
                    if (numStart != std::string::npos) {
                        try {
                            int ver = std::stoi(content.substr(numStart));
                            if (ver > 1) {
                                errorMsg = "The project was created with a newer schema version (" +
                                           std::to_string(ver) +
                                           ") than supported by this release (v1). Please update "
                                           "FluidCore.";
                                return false;
                            }
                        } catch (...) {
                        }
                    }
                }
            }
        }
    }
    return true;
}

void performSaveProjectAs(AppViewContext* ctx);

void confirmDiscardUnsavedChanges(AppViewContext* ctx, std::function<void()> onProceed) {
    if (!ctx)
        return;
    bool dirty = ctx->isProjectDirty || (ctx->workspace && ctx->workspace->canUndo()) ||
                 (ctx->pane && ctx->pane->canUndo());
    if (!dirty) {
        onProceed();
        return;
    }

    GtkWidget* dialog = gtk_message_dialog_new(ctx->window, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE, "Save changes before proceeding?");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "There are unsaved modifications in the current project or workspace. If you don't save, "
        "your changes will be permanently discarded.");

    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Don't Save", GTK_RESPONSE_REJECT);
    GtkWidget* saveBtn =
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save Changes", GTK_RESPONSE_ACCEPT);
    GtkStyleContext* btnCtx = gtk_widget_get_style_context(saveBtn);
    gtk_style_context_add_class(btnCtx, "suggested-action");

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_ACCEPT) {
        if (ctx->engine && ctx->engine->isProjectOpen()) {
            std::string err;
            bool ok = ctx->engine->saveProjectWithError(&err);
            if (ctx->pane) {
                ok = ok && ctx->pane->saveAnnotations();
            }
            if (ok) {
                ctx->isProjectDirty = false;
                if (ctx->headerBar) {
                    ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
                }
                onProceed();
            } else {
                if (ctx->headerBar) {
                    ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
                }
                showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed",
                            "Could not save project: " + err);
            }
        } else {
            ctx->pendingActionProceed = std::move(onProceed);
            performSaveProjectAs(ctx);
        }
    } else if (response == GTK_RESPONSE_REJECT) {
        ctx->isProjectDirty = false;
        onProceed();
    } else {
        ctx->pendingActionProceed = nullptr;
    }
}

void performSaveProjectAs(AppViewContext* ctx) {
    if (!ctx)
        return;

    GtkFileChooserNative* native =
        gtk_file_chooser_native_new("Save Project As (.ltproj Bundle)", ctx->window,
                                    GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER, "_Save", "_Cancel");

    const std::string defaultName = (ctx->engine && !ctx->engine->projectTitle().empty() &&
                                     ctx->engine->projectTitle() != "Untitled Project")
                                        ? ctx->engine->projectTitle() + ".ltproj"
                                        : "Research-Synthesis.ltproj";
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), defaultName.c_str());
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);

    gint res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (res != GTK_RESPONSE_ACCEPT) {
        g_object_unref(native);
        ctx->pendingActionProceed = nullptr;
        return;
    }

    gchar* rawChosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
    g_object_unref(native);
    if (!rawChosen) {
        ctx->pendingActionProceed = nullptr;
        return;
    }

    std::string chosenPath = normalizePath(rawChosen);
    g_free(rawChosen);

    // Enforce .ltproj extension
    if (chosenPath.size() < 7 || chosenPath.substr(chosenPath.size() - 7) != ".ltproj") {
        chosenPath += ".ltproj";
    }

    std::filesystem::path bundlePath(chosenPath);
    std::error_code ec;

    // Create bundle directory and structure
    std::filesystem::create_directories(bundlePath / "documents", ec);
    std::filesystem::create_directories(bundlePath / "assets" / "clips", ec);
    std::filesystem::create_directories(bundlePath / "assets" / "images", ec);
    std::filesystem::create_directories(bundlePath / "cache" / "thumbnails", ec);
    if (ec) {
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Cannot Create Bundle",
                    "Failed to create project bundle directory:\n" + ec.message());
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        ctx->pendingActionProceed = nullptr;
        return;
    }

    // Copy all registered documents into bundle /documents/
    std::vector<std::string> copiedFiles;
    bool copySuccess = true;
    std::string copyError;

    auto allDocs = ctx->pdfDocService ? ctx->pdfDocService->allDocuments()
                                      : std::vector<std::pair<std::string, std::string>>{};

    for (const auto& [docId, origPath] : allDocs) {
        if (origPath.empty())
            continue;
        std::filesystem::path srcPdf(origPath);
        if (!std::filesystem::exists(srcPdf, ec))
            continue;

        std::filesystem::path dstPdf = bundlePath / "documents" / srcPdf.filename();

        if (std::filesystem::canonical(srcPdf, ec) != std::filesystem::canonical(dstPdf, ec)) {
            std::filesystem::copy_file(srcPdf, dstPdf,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                copySuccess = false;
                copyError = "Failed to copy " + srcPdf.filename().string() + ": " + ec.message();
                break;
            }
            copiedFiles.push_back(dstPdf.string());

            std::filesystem::path srcXopp = srcPdf;
            srcXopp += ".xopp";
            if (std::filesystem::exists(srcXopp, ec)) {
                std::filesystem::path dstXopp = dstPdf;
                dstXopp += ".xopp";
                std::filesystem::copy_file(srcXopp, dstXopp,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    copiedFiles.push_back(dstXopp.string());
                }
            }
        }
    }

    if (!copySuccess) {
        for (const auto& f : copiedFiles) {
            std::filesystem::remove(f, ec);
        }
        std::filesystem::remove_all(bundlePath, ec);
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Copy Failed", copyError);
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        ctx->pendingActionProceed = nullptr;
        return;
    }

    // Canonical source switch: repoint document handles to bundle copies
    for (const auto& [docId, origPath] : allDocs) {
        if (origPath.empty())
            continue;
        std::filesystem::path srcPdf(origPath);
        std::filesystem::path dstPdf = bundlePath / "documents" / srcPdf.filename();
        std::string newPath = dstPdf.string();
        if (ctx->pdfDocService) {
            ctx->pdfDocService->repointDocumentPath(docId, newPath);
        }
        if (ctx->pane && ctx->pane->docId() == docId) {
            ctx->pane->repointCompanionPath(newPath);
        }
    }

    // Open/initialize bundle in engine
    std::string err;
    if (!ctx->engine->openProjectWithError(chosenPath, &err)) {
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Database Initialization Failed",
                    "Could not initialize project database: " + err);
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        ctx->pendingActionProceed = nullptr;
        return;
    }

    // Save project
    bool ok = ctx->engine->saveProjectWithError(&err);
    if (ctx->pane) {
        ok = ok && ctx->pane->saveAnnotations();
    }

    if (ok) {
        ctx->isProjectDirty = false;
        if (ctx->workspace)
            ctx->workspace->undoStack().clear();
        if (ctx->pane)
            ctx->pane->undoStack().clear();

        std::filesystem::path stemPath(chosenPath);
        std::string title = stemPath.stem().string();
        ctx->engine->projectStore().setProjectTitle(title);
        if (ctx->headerBar) {
            ctx->headerBar->setProjectTitle(title, chosenPath);
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
        }

        if (ctx->pendingActionProceed) {
            auto proceed = std::move(ctx->pendingActionProceed);
            ctx->pendingActionProceed = nullptr;
            proceed();
        }
    } else {
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed",
                    "Could not save project: " + err);
        ctx->pendingActionProceed = nullptr;
    }
}

void performSaveProject(AppViewContext* ctx) {
    if (!ctx)
        return;
    if (!ctx->engine || !ctx->engine->isProjectOpen()) {
        performSaveProjectAs(ctx);
        return;
    }

    std::string err;
    bool ok = ctx->engine->saveProjectWithError(&err);
    if (ctx->pane) {
        ok = ok && ctx->pane->saveAnnotations();
    }

    if (ok) {
        ctx->isProjectDirty = false;
        if (ctx->workspace)
            ctx->workspace->undoStack().clear();
        if (ctx->pane)
            ctx->pane->undoStack().clear();
        if (ctx->headerBar) {
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
        }
    } else {
        if (ctx->headerBar) {
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        }
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed",
                    "Could not save project: " + err);
    }
}

void performOpenProject(AppViewContext* ctx) {
    if (!ctx)
        return;

    confirmDiscardUnsavedChanges(ctx, [ctx]() {
        GtkFileChooserNative* native =
            gtk_file_chooser_native_new("Open FluidCore Project (.ltproj Bundle)", ctx->window,
                                        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Open", "_Cancel");

        gint res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
        if (res != GTK_RESPONSE_ACCEPT) {
            g_object_unref(native);
            return;
        }

        gchar* rawChosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        g_object_unref(native);
        if (!rawChosen)
            return;

        std::string chosenPath = normalizePath(rawChosen);
        g_free(rawChosen);

        std::string error;
        if (!validateLtprojBundle(chosenPath, error)) {
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Invalid Project Bundle", error);
            return;
        }

        std::string openErr;
        if (!ctx->engine->openProjectWithError(chosenPath, &openErr)) {
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Open Project Failed",
                        "Failed to open project: " + openErr);
            return;
        }

        if (ctx->workspace) {
            ctx->workspace->notifyModelReloaded();
        }

        auto docs = ctx->engine->projectStore().listDocuments();
        if (!docs.empty()) {
            std::filesystem::path bundle(chosenPath);
            std::filesystem::path primaryDoc = bundle / docs[0].relativePath;
            if (ctx->pane) {
                ctx->pane->loadDocument(primaryDoc.string(), docs[0].docId);
            }
            if (ctx->pdfDocService && ctx->pane && ctx->pane->document()) {
                ctx->pdfDocService->clear();
                for (const auto& doc : docs) {
                    std::string dPath = (bundle / doc.relativePath).string();
                    if (doc.docId == docs[0].docId) {
                        ctx->pdfDocService->registerMainDocument(doc.docId, ctx->pane->document(),
                                                                 dPath);
                    } else {
                        ctx->pdfDocService->registerMainDocument(doc.docId, nullptr, dPath);
                    }
                }
            }
        } else {
            if (ctx->pane) {
                ctx->pane->closeDocument();
            }
        }

        ctx->isProjectDirty = false;
        if (ctx->headerBar) {
            ctx->headerBar->setProjectTitle(ctx->engine->projectTitle(), chosenPath);
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
        }
        if (ctx->updateUndoRedoUI) {
            ctx->updateUndoRedoUI();
        }
    });
}

void performOpenPdf(AppViewContext* ctx) {
    if (!ctx)
        return;

    confirmDiscardUnsavedChanges(ctx, [ctx]() {
        GtkFileChooserNative* native = gtk_file_chooser_native_new(
            "Open PDF Document", ctx->window, GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");

        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "PDF Documents (*.pdf)");
        gtk_file_filter_add_pattern(filter, "*.pdf");
        gtk_file_filter_add_mime_type(filter, "application/pdf");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

        gint res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
        if (res != GTK_RESPONSE_ACCEPT) {
            g_object_unref(native);
            return;
        }

        gchar* rawChosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        g_object_unref(native);
        if (!rawChosen)
            return;

        std::string chosenPath = normalizePath(rawChosen);
        g_free(rawChosen);

        if (ctx->pane) {
            bool loaded = ctx->pane->loadDocument(chosenPath);
            if (loaded && ctx->pane->document() && ctx->pdfDocService) {
                ctx->pdfDocService->registerMainDocument(ctx->pane->docId(), ctx->pane->document(),
                                                         chosenPath);
                ctx->pdfDocService->registerMainDocument(chosenPath, ctx->pane->document(),
                                                         chosenPath);
            }
        }

        ctx->isProjectDirty = false;
        if (ctx->headerBar) {
            std::filesystem::path p(chosenPath);
            ctx->headerBar->setProjectTitle(ctx->engine ? ctx->engine->projectTitle()
                                                        : "Untitled Project",
                                            p.filename().string());
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
        }
        if (ctx->updateUndoRedoUI) {
            ctx->updateUndoRedoUI();
        }
    });
}

void performNewProject(AppViewContext* ctx) {
    if (!ctx)
        return;

    confirmDiscardUnsavedChanges(ctx, [ctx]() {
        if (ctx->engine) {
            ctx->engine->newProject("Untitled Project");
        }
        if (ctx->workspace) {
            ctx->workspace->notifyModelReloaded();
        }
        if (ctx->pane) {
            ctx->pane->closeDocument();
        }
        if (ctx->pdfDocService) {
            ctx->pdfDocService->clear();
        }
        ctx->isProjectDirty = false;
        if (ctx->headerBar) {
            ctx->headerBar->setProjectTitle("Untitled Project", "Workspace Canvas");
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
        }
        if (ctx->updateUndoRedoUI) {
            ctx->updateUndoRedoUI();
        }
    });
}

void performExport(AppViewContext* ctx) {
    if (!ctx)
        return;
    FluidCoreApp::ExportDialog::show(ctx->window, ctx->pane, ctx->workspace, ctx->engine);
}

void onActivate(GtkApplication* app, gpointer userData) {
    auto* context = static_cast<AppContext*>(userData);

    // Widgets may only be created after gtk_init(), which happens inside
    // g_application_run() — so views are built here, not in main().
    auto* documentPane = new FluidCoreApp::DocumentPane(*context->pdfPath);
    g_object_set_data_full(
        G_OBJECT(app), "document-pane", documentPane,
        +[](gpointer data) { delete static_cast<FluidCoreApp::DocumentPane*>(data); });

    auto* workspace = new FluidCoreApp::WorkspaceView(*context->engine);
    g_object_set_data_full(
        G_OBJECT(app), "workspace-view", workspace,
        +[](gpointer data) { delete static_cast<FluidCoreApp::WorkspaceView*>(data); });

    // Tool synchronization service & Top modern toolbar
    auto* toolManager = new FluidCoreApp::ToolManager();
    g_object_set_data_full(
        G_OBJECT(app), "tool-manager", toolManager,
        +[](gpointer data) { delete static_cast<FluidCoreApp::ToolManager*>(data); });

    auto* topToolbar = new FluidCoreApp::TopToolbarWidget(*toolManager);
    g_object_set_data_full(
        G_OBJECT(app), "top-toolbar", topToolbar,
        +[](gpointer data) { delete static_cast<FluidCoreApp::TopToolbarWidget*>(data); });

    toolManager->addChangeListener([documentPane, workspace](FluidCoreApp::Tool tool) {
        const char* toolStr = FluidCoreApp::ToolManager::toolToString(tool);
        if (documentPane) {
            documentPane->setTool(toolStr);
        }
        if (workspace) {
            workspace->setTool(toolStr);
        }
    });

    // Multi-document resolution and high-DPI crop tile cache
    auto* pdfDocService = new FluidCoreApp::PdfDocumentService();
    g_object_set_data_full(
        G_OBJECT(app), "pdf-doc-service", pdfDocService,
        +[](gpointer data) { delete static_cast<FluidCoreApp::PdfDocumentService*>(data); });

    if (documentPane->document()) {
        pdfDocService->registerMainDocument(documentPane->docId(), documentPane->document(),
                                            *context->pdfPath);
        pdfDocService->registerMainDocument("doc-primary.pdf", documentPane->document(),
                                            *context->pdfPath);
        if (!context->pdfPath->empty()) {
            pdfDocService->registerMainDocument(*context->pdfPath, documentPane->document(),
                                                *context->pdfPath);
        }
    }

    auto* excerptTileCache = new FluidCoreApp::ExcerptTileCache(*pdfDocService);
    g_object_set_data_full(
        G_OBJECT(app), "excerpt-tile-cache", excerptTileCache,
        +[](gpointer data) { delete static_cast<FluidCoreApp::ExcerptTileCache*>(data); });

    workspace->setExcerptTileCache(excerptTileCache);

    // Wire Bi-Directional Anchor Navigation (TASK-3.3)
    workspace->setNavigateToSourceCallback(
        [documentPane](const std::string& /*docId*/, std::size_t pageNo,
                       const FluidCore::Rectangle& normRect, const std::string& excerptId,
                       const std::string& snippet, const FluidCore::Point& cardCenter) {
            if (documentPane) {
                documentPane->navigateToExcerptSource(pageNo, normRect, excerptId, snippet,
                                                      cardCenter);
            }
        });

    documentPane->setOnReturnToWorkspaceCallback(
        [workspace](const FluidCore::Point& originCoord, const std::string& cardId) {
            if (workspace) {
                workspace->glideToWorldCoord(originCoord.x, originCoord.y);
                workspace->flashExcerptCard(cardId);
            }
        });

    workspace->setOnExcerptAddedCallback([documentPane](const FluidCore::ExcerptCardNode& card) {
        if (documentPane) {
            documentPane->addExcerptAnchor(card);
        }
    });

    // Wire Workspace context to DocumentPane for scoped cross-canvas search (TASK-4.3)
    documentPane->setWorkspaceContext(workspace, context->engine);

    // Sync initial excerpt document source anchors into DocumentPane
    if (context->engine) {
        std::vector<FluidCore::AnchorSpan> excerptAnchors;
        const auto& pages = documentPane->pages();
        for (const auto* node :
             context->engine->queryVisibleNodes(FluidCore::Rectangle{-1e6, -1e6, 2e6, 2e6})) {
            auto* excerpt = dynamic_cast<const FluidCore::ExcerptCardNode*>(node);
            if (excerpt && excerpt->sourcePageNo() < pages.size()) {
                const auto& page = pages[excerpt->sourcePageNo()];
                const auto& srcRect = excerpt->sourceNormalizedRect();
                double y0 = page.y + srcRect.y * page.height;
                double y1 = y0 + srcRect.h * page.height;
                excerptAnchors.push_back(FluidCore::AnchorSpan{y0, y1, "excerpt", 9});
            }
        }
        documentPane->setExcerptAnchors(std::move(excerptAnchors));
    }

    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);

    auto* headerBar = new FluidCoreApp::AppHeaderBar(GTK_WINDOW(window));
    g_object_set_data_full(
        G_OBJECT(app), "app-header-bar", headerBar,
        +[](gpointer data) { delete static_cast<FluidCoreApp::AppHeaderBar*>(data); });

    // Track active pane recency for intelligent undo/redo routing
    auto* lastActivePane = new ActivePane(ActivePane::Workspace);
    g_object_set_data_full(
        G_OBJECT(app), "last-active-pane", lastActivePane,
        +[](gpointer data) { delete static_cast<ActivePane*>(data); });

    AppViewContext* viewCtx = nullptr;

    auto updateUndoRedoUI = [topToolbar, workspace, documentPane, lastActivePane, headerBar,
                             &viewCtx]() {
        bool canUndo = false;
        bool canRedo = false;
        if (*lastActivePane == ActivePane::Document) {
            canUndo = documentPane ? documentPane->canUndo() : false;
            canRedo = documentPane ? documentPane->canRedo() : false;
        } else {
            canUndo = workspace ? workspace->canUndo() : false;
            canRedo = workspace ? workspace->canRedo() : false;
        }
        if (topToolbar) {
            topToolbar->updateUndoRedoState(canUndo, canRedo);
        }
        if (headerBar && viewCtx) {
            bool dirty = viewCtx->isProjectDirty || (workspace ? workspace->canUndo() : false) ||
                         (documentPane ? documentPane->canUndo() : false);
            headerBar->setSaveStatus(dirty ? FluidCoreApp::SaveStatus::Unsaved
                                           : FluidCoreApp::SaveStatus::Saved);
        }
    };

    workspace->undoStack().setChangeListener([lastActivePane, updateUndoRedoUI]() {
        *lastActivePane = ActivePane::Workspace;
        updateUndoRedoUI();
    });
    documentPane->undoStack().setChangeListener([lastActivePane, updateUndoRedoUI]() {
        *lastActivePane = ActivePane::Document;
        updateUndoRedoUI();
    });

    documentPane->setOnActivatedCallback([lastActivePane, updateUndoRedoUI]() {
        if (*lastActivePane != ActivePane::Document) {
            *lastActivePane = ActivePane::Document;
            updateUndoRedoUI();
        }
    });

    workspace->setOnActivatedCallback([lastActivePane, updateUndoRedoUI]() {
        if (*lastActivePane != ActivePane::Workspace) {
            *lastActivePane = ActivePane::Workspace;
            updateUndoRedoUI();
        }
    });

    viewCtx = new AppViewContext{documentPane,   workspace,        toolManager, context->engine,
                                 pdfDocService,  excerptTileCache, headerBar,   GTK_WINDOW(window),
                                 lastActivePane, updateUndoRedoUI, false,       nullptr};
    g_object_set_data_full(
        G_OBJECT(app), "app-view-context", viewCtx,
        +[](gpointer data) { delete static_cast<AppViewContext*>(data); });

    // Wire HeaderBar callbacks
    headerBar->setOnNewProject([viewCtx]() { performNewProject(viewCtx); });
    headerBar->setOnOpenPdf([viewCtx]() { performOpenPdf(viewCtx); });
    headerBar->setOnOpenProject([viewCtx]() { performOpenProject(viewCtx); });
    headerBar->setOnSaveProject([viewCtx]() { performSaveProject(viewCtx); });
    headerBar->setOnSaveProjectAs([viewCtx]() { performSaveProjectAs(viewCtx); });
    headerBar->setOnExport([viewCtx]() { performExport(viewCtx); });

    // Initialize HeaderBar Project Title
    headerBar->setProjectTitle(context->engine ? context->engine->projectTitle()
                                               : "Untitled Project",
                               context->pdfPath->empty() ? "Workspace Canvas" : *context->pdfPath);
    headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);

    // Global window-level event capture to immediately detect clicks/scrolls across Document vs
    // Workspace
    g_signal_connect(
        window, "event", G_CALLBACK(+[](GtkWidget*, GdkEvent* event, gpointer data) -> gboolean {
            auto* ctx = static_cast<AppViewContext*>(data);
            if (!ctx || !event || !ctx->lastActivePane) {
                return FALSE;
            }
            if (event->type == GDK_BUTTON_PRESS || event->type == GDK_2BUTTON_PRESS ||
                event->type == GDK_3BUTTON_PRESS || event->type == GDK_SCROLL ||
                event->type == GDK_TOUCH_BEGIN) {
                GtkWidget* eventWidget = gtk_get_event_widget(event);
                if (eventWidget) {
                    if (ctx->pane && (eventWidget == ctx->pane->widget() ||
                                      gtk_widget_is_ancestor(eventWidget, ctx->pane->widget()))) {
                        if (*ctx->lastActivePane != ActivePane::Document) {
                            *ctx->lastActivePane = ActivePane::Document;
                            if (ctx->updateUndoRedoUI) {
                                ctx->updateUndoRedoUI();
                            }
                        }
                    } else if (ctx->workspace &&
                               (eventWidget == ctx->workspace->widget() ||
                                gtk_widget_is_ancestor(eventWidget, ctx->workspace->widget()))) {
                        if (*ctx->lastActivePane != ActivePane::Workspace) {
                            *ctx->lastActivePane = ActivePane::Workspace;
                            if (ctx->updateUndoRedoUI) {
                                ctx->updateUndoRedoUI();
                            }
                        }
                    }
                }
            }
            return FALSE;
        }),
        viewCtx);

    auto performSmartUndo = [documentPane, workspace, lastActivePane, windowWidget = window,
                             updateUndoRedoUI]() {
        GtkWidget* focusWidget = gtk_window_get_focus(GTK_WINDOW(windowWidget));
        if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
            return;
        }

        if (*lastActivePane == ActivePane::Document) {
            if (documentPane && documentPane->canUndo()) {
                documentPane->undo();
            }
        } else {
            if (workspace && workspace->canUndo()) {
                workspace->undo();
            }
        }
        updateUndoRedoUI();
    };

    auto performSmartRedo = [documentPane, workspace, lastActivePane, windowWidget = window,
                             updateUndoRedoUI]() {
        GtkWidget* focusWidget = gtk_window_get_focus(GTK_WINDOW(windowWidget));
        if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
            return;
        }

        if (*lastActivePane == ActivePane::Document) {
            if (documentPane && documentPane->canRedo()) {
                documentPane->redo();
            }
        } else {
            if (workspace && workspace->canRedo()) {
                workspace->redo();
            }
        }
        updateUndoRedoUI();
    };

    // Wire TopToolbar callbacks
    topToolbar->setOnUndo([performSmartUndo]() { performSmartUndo(); });
    topToolbar->setOnRedo([performSmartRedo]() { performSmartRedo(); });

    topToolbar->setOnZoomIn([workspace, documentPane, lastActivePane]() {
        if (*lastActivePane == ActivePane::Workspace) {
            GtkAllocation alloc;
            gtk_widget_get_allocation(workspace->widget(), &alloc);
            const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
            const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
            workspace->zoomAt(1.2, cx, cy);
        } else {
            documentPane->zoomIn();
        }
    });

    topToolbar->setOnZoomOut([workspace, documentPane, lastActivePane]() {
        if (*lastActivePane == ActivePane::Workspace) {
            GtkAllocation alloc;
            gtk_widget_get_allocation(workspace->widget(), &alloc);
            const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
            const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
            workspace->zoomAt(0.8333, cx, cy);
        } else {
            documentPane->zoomOut();
        }
    });

    topToolbar->setOnResetView([workspace, documentPane, lastActivePane]() {
        if (*lastActivePane == ActivePane::Workspace) {
            workspace->resetView();
        } else {
            documentPane->resetZoom();
        }
    });

    topToolbar->setOnToggleMinimap([workspace, topToolbar]() {
        const bool newVisible = !workspace->isMinimapVisible();
        workspace->setMinimapVisible(newVisible);
        topToolbar->setMinimapActive(newVisible);
    });

    topToolbar->setOnSearch([documentPane]() {
        if (documentPane) {
            documentPane->openSearch(false, FluidCoreApp::SearchScope::All);
        }
    });

    topToolbar->setOnExport([viewCtx]() { performExport(viewCtx); });

    // Wire Project Management Actions
    GSimpleAction* newAction = g_simple_action_new("new_project", nullptr);
    g_signal_connect(newAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performNewProject(static_cast<AppViewContext*>(data));
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(newAction));
    const gchar* newAccels[] = {"<Primary>n", "<Control>n", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.new_project", newAccels);

    GSimpleAction* openPdfAction = g_simple_action_new("open_pdf", nullptr);
    g_signal_connect(openPdfAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performOpenPdf(static_cast<AppViewContext*>(data));
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(openPdfAction));
    const gchar* openPdfAccels[] = {"<Primary>o", "<Control>o", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.open_pdf", openPdfAccels);

    GSimpleAction* openProjectAction = g_simple_action_new("open_project", nullptr);
    g_signal_connect(openProjectAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performOpenProject(static_cast<AppViewContext*>(data));
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(openProjectAction));
    const gchar* openProjAccels[] = {"<Primary><Shift>o", "<Control><Shift>o", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.open_project", openProjAccels);

    // Wire Ctrl+S accelerator to save project & annotations
    GSimpleAction* saveAction = g_simple_action_new("save", nullptr);
    g_signal_connect(saveAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performSaveProject(static_cast<AppViewContext*>(data));
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(saveAction));
    const gchar* saveAccels[] = {"<Primary>s", "<Control>s", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.save", saveAccels);

    // Wire Ctrl+Shift+S (Save As) action
    GSimpleAction* saveAsAction = g_simple_action_new("save_as", nullptr);
    g_signal_connect(saveAsAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performSaveProjectAs(static_cast<AppViewContext*>(data));
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(saveAsAction));
    const gchar* saveAsAccels[] = {"<Primary><Shift>s", "<Control><Shift>s", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.save_as", saveAsAccels);

    // Wire Ctrl+Z (Undo) action
    GSimpleAction* undoAction = g_simple_action_new("undo", nullptr);
    g_signal_connect(undoAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* fn = static_cast<std::function<void()>*>(data);
                         if (fn && *fn) {
                             (*fn)();
                         }
                     }),
                     new std::function<void()>(performSmartUndo));
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(undoAction));
    const gchar* undoAccels[] = {"<Primary>z", "<Control>z", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.undo", undoAccels);

    // Wire Ctrl+Shift+Z / Ctrl+Y (Redo) action
    GSimpleAction* redoAction = g_simple_action_new("redo", nullptr);
    g_signal_connect(redoAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* fn = static_cast<std::function<void()>*>(data);
                         if (fn && *fn) {
                             (*fn)();
                         }
                     }),
                     new std::function<void()>(performSmartRedo));
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(redoAction));
    const gchar* redoAccels[] = {"<Primary><Shift>z", "<Control><Shift>z", "<Primary>y",
                                 "<Control>y", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.redo", redoAccels);

    // Wire Ctrl+C (Copy) action
    GSimpleAction* copyAction = g_simple_action_new("copy", nullptr);
    g_signal_connect(copyAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->copySelection();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(copyAction));
    const gchar* copyAccels[] = {"<Primary>c", "<Control>c", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.copy", copyAccels);

    // Wire Ctrl+E (Export) action
    GSimpleAction* exportAction = g_simple_action_new("export", nullptr);
    g_signal_connect(exportAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performExport(static_cast<AppViewContext*>(data));
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(exportAction));
    const gchar* exportAccels[] = {"<Primary>e", "<Control>e", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.export", exportAccels);

    // Wire tool switching actions
    GSimpleAction* penAction = g_simple_action_new("tool_pen", nullptr);
    g_signal_connect(penAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->toolManager) {
                             ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Pen);
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(penAction));
    const gchar* penAccels[] = {"<Alt>1", "F1", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_pen", penAccels);

    GSimpleAction* highlighterAction = g_simple_action_new("tool_highlighter", nullptr);
    g_signal_connect(highlighterAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->toolManager) {
                             ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Highlighter);
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(highlighterAction));
    const gchar* highlighterAccels[] = {"<Alt>2", "F2", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_highlighter",
                                          highlighterAccels);

    GSimpleAction* eraserAction = g_simple_action_new("tool_eraser", nullptr);
    g_signal_connect(eraserAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->toolManager) {
                             ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Eraser);
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(eraserAction));
    const gchar* eraserAccels[] = {"<Alt>3", "F3", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_eraser", eraserAccels);

    GSimpleAction* selectAction = g_simple_action_new("tool_select", nullptr);
    g_signal_connect(selectAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->toolManager) {
                             ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Select);
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(selectAction));
    const gchar* selectAccels[] = {"<Alt>4", "F4", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_select", selectAccels);

    GSimpleAction* cropAction = g_simple_action_new("tool_crop", nullptr);
    g_signal_connect(cropAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->toolManager) {
                             ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Crop);
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(cropAction));
    const gchar* cropAccels[] = {"<Alt>5", "F5", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_crop", cropAccels);

    GSimpleAction* connectorAction = g_simple_action_new("tool_connector", nullptr);
    g_signal_connect(connectorAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (ctx && ctx->toolManager) {
                             ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Connector);
                         }
                     }),
                     viewCtx);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(connectorAction));
    const gchar* connectorAccels[] = {"<Alt>6", "F6", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.tool_connector",
                                          connectorAccels);

    // Wire Ctrl+Shift+0 (Reset Squeeze) action
    GSimpleAction* resetSqueezeAction = g_simple_action_new("reset_squeeze", nullptr);
    g_signal_connect(resetSqueezeAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->resetSqueeze();
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(resetSqueezeAction));
    const gchar* resetSqueezeAccels[] = {"<Primary><Shift>0", "<Primary><Shift>parenright",
                                         "<Control><Shift>0", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.reset_squeeze",
                                          resetSqueezeAccels);

    // Wire Document Squeeze Search, Canvas Search, All Search actions
    GSimpleAction* searchSqueezeAction = g_simple_action_new("search_squeeze", nullptr);
    g_signal_connect(searchSqueezeAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->openSearch(true, FluidCoreApp::SearchScope::Document);
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(searchSqueezeAction));
    const gchar* searchSqueezeAccels[] = {"<Primary><Alt>s", "<Control><Alt>s", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.search_squeeze",
                                          searchSqueezeAccels);

    GSimpleAction* canvasSearchAction = g_simple_action_new("canvas_search", nullptr);
    g_signal_connect(canvasSearchAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->openSearch(false, FluidCoreApp::SearchScope::Workspace);
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(canvasSearchAction));
    const gchar* canvasSearchAccels[] = {"<Primary><Shift>f", "<Control><Shift>f", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.canvas_search",
                                          canvasSearchAccels);

    GSimpleAction* allSearchAction = g_simple_action_new("all_search", nullptr);
    g_signal_connect(allSearchAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         auto* pane = static_cast<FluidCoreApp::DocumentPane*>(data);
                         if (pane) {
                             pane->openSearch(false, FluidCoreApp::SearchScope::All);
                         }
                     }),
                     documentPane);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(allSearchAction));
    const gchar* allSearchAccels[] = {"<Primary><Alt>f", "<Control><Alt>f", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.all_search", allSearchAccels);

    // Global Key Press Event Filter
    g_signal_connect(
        window, "key-press-event",
        G_CALLBACK(+[](GtkWidget* windowWidget, GdkEventKey* event, gpointer data) -> gboolean {
            auto* ctx = static_cast<AppViewContext*>(data);
            if (!ctx)
                return FALSE;

            auto* pane = ctx->pane;
            auto* ws = ctx->workspace;
            auto* tm = ctx->toolManager;

            const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
            const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
            const bool alt = (event->state & GDK_MOD1_MASK) != 0;

            if (ctrl && !shift && (event->keyval == GDK_KEY_n || event->keyval == GDK_KEY_N)) {
                performNewProject(ctx);
                return TRUE;
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_o || event->keyval == GDK_KEY_O)) {
                performOpenPdf(ctx);
                return TRUE;
            }
            if (ctrl && shift && (event->keyval == GDK_KEY_o || event->keyval == GDK_KEY_O)) {
                performOpenProject(ctx);
                return TRUE;
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
                performSaveProject(ctx);
                return TRUE;
            }
            if (ctrl && shift && (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
                performSaveProjectAs(ctx);
                return TRUE;
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_e || event->keyval == GDK_KEY_E)) {
                performExport(ctx);
                return TRUE;
            }

            // Quick Esc handling
            if (event->keyval == GDK_KEY_Escape) {
                if (ws) {
                    ws->cancelCurrentInteraction();
                }
                if (pane) {
                    pane->clearTextSelection();
                    pane->clearCropSelection();
                }
                return TRUE;
            }

            if (ctrl && shift &&
                (event->keyval == GDK_KEY_0 || event->keyval == GDK_KEY_parenright ||
                 event->keyval == GDK_KEY_KP_0 || event->keyval == GDK_KEY_r ||
                 event->keyval == GDK_KEY_R)) {
                if (pane) {
                    pane->resetSqueeze();
                    return TRUE;
                }
            }
            if (ctrl && shift && (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H)) {
                if (pane) {
                    pane->toggleHighlightView();
                    return TRUE;
                }
            }
            if (ctrl && shift && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
                if (pane) {
                    pane->openSearch(false, FluidCoreApp::SearchScope::Workspace);
                    return TRUE;
                }
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
                if (pane) {
                    pane->openSearch(false, FluidCoreApp::SearchScope::Document);
                    return TRUE;
                }
            }

            // Quick single-key tool switching when no modifier is held
            if (!ctrl && !alt && !shift) {
                GtkWidget* focusWidget = gtk_window_get_focus(GTK_WINDOW(windowWidget));
                if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
                    return FALSE;
                }

                if (event->keyval == GDK_KEY_space) {
                    if (ws)
                        ws->setSpacePressed(true);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Select);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_p || event->keyval == GDK_KEY_P) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Pen);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Highlighter);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_e || event->keyval == GDK_KEY_E) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Eraser);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Crop);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_l || event->keyval == GDK_KEY_L) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Connector);
                    return TRUE;
                }
            }

            return FALSE;
        }),
        viewCtx);

    // Key Release Event Filter for Space panning
    g_signal_connect(window, "key-release-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         auto* ctx = static_cast<AppViewContext*>(data);
                         if (!ctx)
                             return FALSE;
                         if (event->keyval == GDK_KEY_space) {
                             if (ctx->workspace) {
                                 ctx->workspace->setSpacePressed(false);
                             }
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     viewCtx);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* documentWidget = documentPane->widget();
    gtk_widget_set_size_request(documentWidget, 360, -1);
    gtk_paned_pack1(GTK_PANED(paned), documentWidget, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), workspace->widget(), TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 480);

    GtkWidget* rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), topToolbar->widget(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), paned, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), rootBox);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_urgency_hint(GTK_WINDOW(window), TRUE);
    gtk_widget_show_all(window);
    gtk_window_deiconify(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(window));

    updateUndoRedoUI();

    std::cout << "[FluidCore] Window ready and presented (" << documentPane->pages().size()
              << " pages loaded)." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    const std::string rawArg = argc > 1 ? argv[1] : "";
    const std::string pdfPath = normalizePath(rawArg);

    FluidCoreEngine engine("default-project");
    seedDemoContent(engine, pdfPath);

    AppContext context{&engine, &pdfPath};

    std::cout << "[FluidCore] Starting application with document: "
              << (pdfPath.empty() ? "(none)" : pdfPath) << std::endl;

    GtkApplication* app = gtk_application_new("org.fluidcore.platform", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &context);
    const int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return status;
}
