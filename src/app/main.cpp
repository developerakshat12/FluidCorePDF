#include "FluidCoreEngine.h"
#include "MemoryTelemetry.h"
#include "document/DocumentPane.h"
#include "export/ExportDialog.h"
#include "geometry/StrokeHitTest.h"
#include "input/PalmRejectionEngine.h"
#include "services/ExcerptTileCache.h"
#include "services/PdfDocumentService.h"
#include "services/ToolManager.h"
#include "window/AppHeaderBar.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/TopToolbarWidget.h"
#include "workspace/WorkspaceView.h"

#ifdef FLUIDCORE_HAS_MIMALLOC
#include <mimalloc.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#include <windows.h>
#endif

#ifndef _WIN32
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

static void crashSignalHandler(int sig) {
    const char* sigName = (sig == SIGSEGV)   ? "SIGSEGV (Segmentation fault)"
                          : (sig == SIGABRT) ? "SIGABRT (Abort)"
                          : (sig == SIGBUS)  ? "SIGBUS (Bus error)"
                          : (sig == SIGFPE)  ? "SIGFPE (Floating point exception)"
                          : (sig == SIGILL)  ? "SIGILL (Illegal instruction)"
                                             : "UNKNOWN SIGNAL";
    std::cerr << "\n================ [FluidCore FATAL CRASH] ================\n"
              << "Caught fatal signal: " << sigName << " (" << sig << ")\n";
    void* callstack[64];
    int frames = backtrace(callstack, 64);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
    std::cerr << "=========================================================\n" << std::endl;
    _exit(128 + sig);
}

static void installCrashHandlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crashSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
}
#endif

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
    std::error_code ec;
    std::filesystem::path p(path);
    if (std::filesystem::exists(p, ec)) {
        p = std::filesystem::absolute(p, ec);
        path = p.string();
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
#else
    // On native Windows, convert backslashes to forward slashes for URI/GLib consistency
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
#endif
    return path;
}

std::string resolveLtprojPath(const std::string& inputPath) {
    std::filesystem::path p(inputPath);
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) {
        if (p.filename() == "project.db" || p.filename() == "metadata.json") {
            return p.parent_path().string();
        }
    }
    return inputPath;
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
    const std::string* projectPath = nullptr;
    bool runScenarioA = false;
    bool runScenarioB = false;
    bool runScenarioRepeatedFind = false;
    bool runScenarioReopenAudit = false;
    int repeatedFindIterations = 20;
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
    bool runScenarioA = false;
    bool runScenarioB = false;
    bool runScenarioRepeatedFind = false;
    bool runScenarioReopenAudit = false;
    GtkApplication* app = nullptr;
    int repeatedFindIterations = 20;
};

bool validateLtprojBundle(const std::string& path, std::string& errorMsg) {
    std::filesystem::path p(path);
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec) &&
        (p.filename() == "project.db" || p.filename() == "metadata.json")) {
        p = p.parent_path();
    }
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

void performSaveProject(AppViewContext* ctx);
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
            performSaveProject(ctx);
        }
    } else if (response == GTK_RESPONSE_REJECT) {
        ctx->isProjectDirty = false;
        onProceed();
    } else {
        ctx->pendingActionProceed = nullptr;
    }
}

void configureNativeFileChooser(GtkFileChooserNative* native, AppViewContext* ctx) {
    if (!native)
        return;
    if (ctx && ctx->pane && !ctx->pane->pdfPath().empty()) {
        std::error_code ec;
        std::filesystem::path curDir = std::filesystem::path(ctx->pane->pdfPath()).parent_path();
        if (std::filesystem::exists(curDir, ec)) {
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), curDir.string().c_str());
            return;
        }
    }
#ifndef G_OS_WIN32
    if (std::filesystem::exists("/mnt/d")) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), "/mnt/d");
    } else if (std::filesystem::exists("/mnt/c")) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), "/mnt/c");
    }
#else
    std::error_code ec;
    if (std::filesystem::exists("D:\\", ec)) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), "D:\\");
    } else if (std::filesystem::exists("C:\\", ec)) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(native), "C:\\");
    }
#endif
}

void performSaveProjectAs(AppViewContext* ctx) {
    if (!ctx)
        return;

    try {
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Save Project As (.ltproj Bundle)", ctx->window, GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);

        if (ctx->pane && !ctx->pane->pdfPath().empty()) {
            std::error_code ec;
            std::filesystem::path curDir =
                std::filesystem::path(ctx->pane->pdfPath()).parent_path();
            if (std::filesystem::exists(curDir, ec)) {
                gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                                    curDir.string().c_str());
            }
        }

        const std::string defaultName = (ctx->engine && !ctx->engine->projectTitle().empty() &&
                                         ctx->engine->projectTitle() != "Untitled Project")
                                            ? ctx->engine->projectTitle() + ".ltproj"
                                            : "Research-Synthesis.ltproj";
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), defaultName.c_str());
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

        gint res = gtk_dialog_run(GTK_DIALOG(dialog));
        if (res != GTK_RESPONSE_ACCEPT) {
            gtk_widget_destroy(dialog);
            ctx->pendingActionProceed = nullptr;
            return;
        }

        gchar* rawChosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_widget_destroy(dialog);
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
        if (ec) {
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Cannot Create Bundle",
                        "Failed to create project bundle directory:\n" + ec.message());
            if (ctx->headerBar)
                ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
            ctx->pendingActionProceed = nullptr;
            return;
        }
        std::filesystem::create_directories(bundlePath / "assets" / "clips", ec);
        std::filesystem::create_directories(bundlePath / "assets" / "images", ec);
        std::filesystem::create_directories(bundlePath / "cache" / "thumbnails", ec);
        ec.clear();

        // Collect all documents and deduplicate by source file path
        auto allDocs = ctx->pdfDocService ? ctx->pdfDocService->allDocuments()
                                          : std::vector<std::pair<std::string, std::string>>{};

        // Ensure pane's active document is present in allDocs
        if (ctx->pane && !ctx->pane->pdfPath().empty()) {
            bool found = false;
            for (const auto& [docId, p] : allDocs) {
                if (p == ctx->pane->pdfPath()) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                allDocs.emplace_back(ctx->pane->docId(), ctx->pane->pdfPath());
            }
        }

        // Map: canonical source path -> destination filename
        std::unordered_map<std::string, std::string> copiedPathToDstFilename;

        for (const auto& [docId, origPath] : allDocs) {
            if (origPath.empty())
                continue;
            std::filesystem::path srcPdf(origPath);
            if (!std::filesystem::exists(srcPdf, ec))
                continue;

            std::string srcKey = srcPdf.string();
            if (copiedPathToDstFilename.find(srcKey) != copiedPathToDstFilename.end()) {
                continue;
            }

            std::string filename = srcPdf.filename().string();
            std::filesystem::path dstXopp = bundlePath / "documents" / filename;
            dstXopp.replace_extension(".xopp");

            // Migrate any pre-existing companion .xopp into bundle if present alongside srcPdf
            std::filesystem::path srcXopp1 = srcPdf;
            srcXopp1.replace_extension(".xopp");
            std::filesystem::path srcXopp2 = srcPdf.string() + ".xopp";

            if (!std::filesystem::exists(dstXopp, ec)) {
                if (std::filesystem::exists(srcXopp1, ec)) {
                    std::filesystem::copy_file(
                        srcXopp1, dstXopp, std::filesystem::copy_options::overwrite_existing, ec);
                } else if (std::filesystem::exists(srcXopp2, ec)) {
                    std::filesystem::copy_file(
                        srcXopp2, dstXopp, std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
            ec.clear();

            copiedPathToDstFilename[srcKey] = filename;
        }

        // Initialize bundle database
        std::string err;
        if (!ctx->engine->projectStore().openProject(chosenPath, &err)) {
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Database Initialization Failed",
                        "Could not initialize project database: " + err);
            if (ctx->headerBar)
                ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
            ctx->pendingActionProceed = nullptr;
            return;
        }

        // Register all active documents in projectStore
        const uint64_t now =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());

        std::unordered_set<std::string> registeredDocIds;
        for (const auto& [docId, origPath] : allDocs) {
            if (docId.empty())
                continue;
            std::filesystem::path srcPdf(origPath);
            std::string filename = srcPdf.filename().string();
            if (filename.empty()) {
                filename = "document.pdf";
            }
            std::string relativePath = "documents/" + filename;
            std::string absExtPath;
            if (!origPath.empty() && std::filesystem::exists(srcPdf, ec)) {
                absExtPath = std::filesystem::absolute(srcPdf, ec).lexically_normal().string();
            }
            ec.clear();

            size_t pageCount = (ctx->pane && ctx->pane->document() &&
                                (ctx->pane->docId() == docId || ctx->pane->pdfPath() == origPath))
                                   ? ctx->pane->pages().size()
                                   : 1;
            size_t fileSizeBytes = 0;
            if (!absExtPath.empty() && std::filesystem::exists(absExtPath, ec)) {
                fileSizeBytes = std::filesystem::file_size(absExtPath, ec);
            }
            ec.clear();

            FluidCore::DocumentRecord rec;
            rec.docId = docId;
            rec.filename = filename;
            rec.relativePath = relativePath;
            rec.externalPath = absExtPath;
            rec.sha256 = "sha256-placeholder";
            rec.pageCount = pageCount > 0 ? pageCount : 1;
            rec.fileSizeBytes = fileSizeBytes;
            rec.createdAt = now;
            if (ctx->pane && ctx->pane->document() &&
                (ctx->pane->docId() == docId || ctx->pane->pdfPath() == origPath)) {
                rec.lastViewedPage = ctx->pane->currentPage();
            }

            ctx->engine->projectStore().registerDocument(rec, nullptr);
            registeredDocIds.insert(docId);
        }

        // Migrate image cards and register any missing document nodes in projectStore
        if (ctx->engine) {
            std::filesystem::path dstAssetsDir = bundlePath / "assets" / "images";
            std::filesystem::create_directories(dstAssetsDir, ec);
            ec.clear();

            auto ensureDocAndImages = [&](auto& self, FluidCore::WorkspaceNode* node) -> void {
                if (!node)
                    return;
                if (auto* card = dynamic_cast<FluidCore::ExcerptCardNode*>(node)) {
                    if (card->isImageExcerpt()) {
                        std::string docId = card->sourceDocId();
                        std::filesystem::path imgPath(docId);
                        std::string imgFilename = imgPath.filename().string();
                        if (imgFilename.empty()) {
                            imgFilename = "image.png";
                        }
                        std::filesystem::path dstImg = dstAssetsDir / imgFilename;

                        // Check if file exists at imgPath or session assets
                        std::filesystem::path srcImgPath = imgPath;
                        if (!std::filesystem::exists(srcImgPath, ec)) {
                            std::filesystem::path sessionCandidate =
                                std::filesystem::temp_directory_path() / "FluidCore" /
                                "session_assets" / "assets" / "images" / imgFilename;
                            if (std::filesystem::exists(sessionCandidate, ec)) {
                                srcImgPath = sessionCandidate;
                            }
                        }
                        ec.clear();

                        if (std::filesystem::exists(srcImgPath, ec)) {
                            bool same = std::filesystem::exists(dstImg, ec) &&
                                        std::filesystem::equivalent(srcImgPath, dstImg, ec);
                            ec.clear();
                            if (!same) {
                                std::filesystem::copy_file(
                                    srcImgPath, dstImg,
                                    std::filesystem::copy_options::overwrite_existing, ec);
                                ec.clear();
                            }
                        }

                        std::string relPath = "assets/images/" + imgFilename;
                        card->setSourceDocId(relPath);

                        FluidCore::DocumentRecord imgRec;
                        imgRec.docId = relPath;
                        imgRec.filename = imgFilename;
                        imgRec.relativePath = relPath;
                        imgRec.externalPath = "";
                        imgRec.pageCount = 1;
                        imgRec.createdAt = now;
                        ctx->engine->projectStore().registerDocument(imgRec, nullptr);
                        registeredDocIds.insert(relPath);
                    } else {
                        const std::string& cardDocId = card->sourceDocId();
                        if (!cardDocId.empty() &&
                            registeredDocIds.find(cardDocId) == registeredDocIds.end()) {
                            std::string resolvedPath;
                            if (ctx->pdfDocService) {
                                resolvedPath = ctx->pdfDocService->getFilePath(cardDocId);
                            }
                            if (resolvedPath.empty() && ctx->pane) {
                                resolvedPath = ctx->pane->pdfPath();
                            }
                            std::filesystem::path p(resolvedPath.empty() ? cardDocId
                                                                         : resolvedPath);
                            std::string filename = p.filename().string();
                            if (filename.empty()) {
                                filename = "document.pdf";
                            }
                            std::string absPath;
                            if (!resolvedPath.empty() && std::filesystem::exists(p, ec)) {
                                absPath =
                                    std::filesystem::absolute(p, ec).lexically_normal().string();
                            }
                            ec.clear();

                            FluidCore::DocumentRecord rec;
                            rec.docId = cardDocId;
                            rec.filename = filename;
                            rec.relativePath = "documents/" + filename;
                            rec.externalPath = absPath;
                            rec.sha256 = "sha256-placeholder";
                            rec.pageCount = card->sourcePageNo() + 1;
                            rec.fileSizeBytes = 0;
                            rec.createdAt = now;

                            ctx->engine->projectStore().registerDocument(rec, nullptr);
                            registeredDocIds.insert(cardDocId);
                        }
                    }
                } else if (auto* stack = dynamic_cast<FluidCore::CardStackNode*>(node)) {
                    for (const auto& child : stack->children()) {
                        self(self, child.get());
                    }
                }
            };

            for (const std::string& nId : ctx->engine->workspaceModel().allNodeIds()) {
                ensureDocAndImages(ensureDocAndImages, ctx->engine->workspaceModel().find(nId));
            }
        }

        // Set companion persistence path for active documents
        for (const auto& [docId, origPath] : allDocs) {
            if (origPath.empty())
                continue;
            std::filesystem::path srcPdf(origPath);
            std::string filename = srcPdf.filename().string();
            if (filename.empty()) {
                filename = "document.pdf";
            }
            std::filesystem::path dstXopp = bundlePath / "documents" / filename;
            dstXopp.replace_extension(".xopp");

            if (ctx->pane && (ctx->pane->docId() == docId || ctx->pane->pdfPath() == origPath)) {
                ctx->pane->setCompanionPath(dstXopp.string());
            }
        }

        // Execute save
        bool ok = ctx->engine->saveProjectWithError(&err);
        if (ctx->pane) {
            ok = ok && ctx->pane->saveAnnotations();
        }

        if (ok) {

            if (ctx->workspace) {
                ctx->workspace->setProjectBundlePath(chosenPath);
                ctx->workspace->undoStack().clear();
            }
            if (ctx->pane)
                ctx->pane->undoStack().clear();
            ctx->isProjectDirty = false;

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
    } catch (const std::exception& ex) {
        std::cerr << "[FluidCore] Exception in performSaveProjectAs: " << ex.what() << std::endl;
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed", ex.what());
        ctx->pendingActionProceed = nullptr;
    } catch (...) {
        std::cerr << "[FluidCore] Unknown exception in performSaveProjectAs" << std::endl;
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed",
                    "An unexpected error occurred while saving.");
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

    try {
        std::filesystem::path bundlePath(ctx->engine->projectPath());
        std::error_code ec;

        // Incremental document copy: ensure all active and referenced documents exist in
        // bundle/documents/
        auto allDocs = ctx->pdfDocService ? ctx->pdfDocService->allDocuments()
                                          : std::vector<std::pair<std::string, std::string>>{};
        if (ctx->pane && !ctx->pane->pdfPath().empty()) {
            bool found = false;
            for (const auto& [docId, p] : allDocs) {
                if (p == ctx->pane->pdfPath()) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                allDocs.emplace_back(ctx->pane->docId(), ctx->pane->pdfPath());
            }
        }

        std::unordered_set<std::string> knownDocIds;
        for (const auto& d : ctx->engine->projectStore().listDocuments()) {
            knownDocIds.insert(d.docId);
        }

        const uint64_t now =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());

        for (const auto& [docId, origPath] : allDocs) {
            if (origPath.empty())
                continue;
            std::filesystem::path srcPdf(origPath);
            if (!std::filesystem::exists(srcPdf, ec))
                continue;

            std::string filename = srcPdf.filename().string();
            if (filename.empty()) {
                filename = "document.pdf";
            }
            std::filesystem::path dstXopp = bundlePath / "documents" / filename;
            dstXopp.replace_extension(".xopp");

            std::string absExtPath;
            if (!origPath.empty() && std::filesystem::exists(srcPdf, ec)) {
                absExtPath = std::filesystem::absolute(srcPdf, ec).lexically_normal().string();
            }
            ec.clear();

            if (ctx->pane && (ctx->pane->docId() == docId || ctx->pane->pdfPath() == origPath)) {
                ctx->pane->setCompanionPath(dstXopp.string());
            }

            size_t fileSizeBytes = 0;
            if (!absExtPath.empty() && std::filesystem::exists(absExtPath, ec)) {
                fileSizeBytes = std::filesystem::file_size(absExtPath, ec);
            }
            ec.clear();

            auto existingDoc = ctx->engine->projectStore().getDocument(docId);
            FluidCore::DocumentRecord rec;
            if (existingDoc) {
                rec = *existingDoc;
            } else {
                rec.docId = docId;
                rec.filename = filename;
                rec.relativePath = "documents/" + filename;
                rec.externalPath = absExtPath;
                rec.sha256 = "sha256-placeholder";
                rec.createdAt = now;
            }
            rec.filename = filename;
            rec.relativePath = "documents/" + filename;
            rec.externalPath = absExtPath;
            rec.fileSizeBytes = fileSizeBytes;
            if (ctx->pane && ctx->pane->document() &&
                (ctx->pane->docId() == docId || ctx->pane->pdfPath() == origPath)) {
                rec.pageCount = ctx->pane->pages().size();
                rec.lastViewedPage = ctx->pane->currentPage();
            } else if (rec.pageCount == 0) {
                rec.pageCount = 1;
            }
            ctx->engine->projectStore().registerDocument(rec, nullptr);
            knownDocIds.insert(docId);
        }

        // Migrate image cards and register any missing document nodes in projectStore
        if (ctx->engine) {
            std::filesystem::path dstAssetsDir = bundlePath / "assets" / "images";
            std::filesystem::create_directories(dstAssetsDir, ec);
            ec.clear();

            auto ensureDocAndImages = [&](auto& self, FluidCore::WorkspaceNode* node) -> void {
                if (!node)
                    return;
                if (auto* card = dynamic_cast<FluidCore::ExcerptCardNode*>(node)) {
                    if (card->isImageExcerpt()) {
                        std::string docId = card->sourceDocId();
                        std::filesystem::path imgPath(docId);
                        std::string imgFilename = imgPath.filename().string();
                        if (imgFilename.empty()) {
                            imgFilename = "image.png";
                        }
                        std::filesystem::path dstImg = dstAssetsDir / imgFilename;

                        // Check if file exists at imgPath or session assets
                        std::filesystem::path srcImgPath = imgPath;
                        if (!std::filesystem::exists(srcImgPath, ec)) {
                            std::filesystem::path sessionCandidate =
                                std::filesystem::temp_directory_path() / "FluidCore" /
                                "session_assets" / "assets" / "images" / imgFilename;
                            if (std::filesystem::exists(sessionCandidate, ec)) {
                                srcImgPath = sessionCandidate;
                            }
                        }
                        ec.clear();

                        if (std::filesystem::exists(srcImgPath, ec)) {
                            bool same = std::filesystem::exists(dstImg, ec) &&
                                        std::filesystem::equivalent(srcImgPath, dstImg, ec);
                            ec.clear();
                            if (!same) {
                                std::filesystem::copy_file(
                                    srcImgPath, dstImg,
                                    std::filesystem::copy_options::overwrite_existing, ec);
                                ec.clear();
                            }
                        }

                        std::string relPath = "assets/images/" + imgFilename;
                        card->setSourceDocId(relPath);

                        FluidCore::DocumentRecord imgRec;
                        imgRec.docId = relPath;
                        imgRec.filename = imgFilename;
                        imgRec.relativePath = relPath;
                        imgRec.externalPath = "";
                        imgRec.pageCount = 1;
                        imgRec.createdAt = now;
                        ctx->engine->projectStore().registerDocument(imgRec, nullptr);
                        knownDocIds.insert(relPath);
                    } else {
                        const std::string& cardDocId = card->sourceDocId();
                        if (!cardDocId.empty() &&
                            knownDocIds.find(cardDocId) == knownDocIds.end()) {
                            std::string resPath = ctx->pdfDocService
                                                      ? ctx->pdfDocService->getFilePath(cardDocId)
                                                      : "";
                            if (resPath.empty() && ctx->pane) {
                                resPath = ctx->pane->pdfPath();
                            }
                            std::filesystem::path p(resPath.empty() ? cardDocId : resPath);
                            std::string filename = p.filename().string();
                            if (filename.empty())
                                filename = "document.pdf";
                            std::string absPath;
                            if (!resPath.empty() && std::filesystem::exists(p, ec)) {
                                absPath =
                                    std::filesystem::absolute(p, ec).lexically_normal().string();
                            }
                            ec.clear();

                            FluidCore::DocumentRecord rec;
                            rec.docId = cardDocId;
                            rec.filename = filename;
                            rec.relativePath = "documents/" + filename;
                            rec.externalPath = absPath;
                            rec.sha256 = "sha256-placeholder";
                            rec.pageCount = card->sourcePageNo() + 1;
                            rec.fileSizeBytes = 0;
                            rec.createdAt = now;

                            ctx->engine->projectStore().registerDocument(rec, nullptr);
                            knownDocIds.insert(cardDocId);
                        }
                    }
                } else if (auto* stack = dynamic_cast<FluidCore::CardStackNode*>(node)) {
                    for (const auto& child : stack->children()) {
                        self(self, child.get());
                    }
                }
            };

            for (const std::string& nId : ctx->engine->workspaceModel().allNodeIds()) {
                ensureDocAndImages(ensureDocAndImages, ctx->engine->workspaceModel().find(nId));
            }
        }

        std::string err;
        bool ok = ctx->engine->saveProjectWithError(&err);
        if (ctx->pane) {
            ok = ok && ctx->pane->saveAnnotations();
        }

        if (ok) {
            if (ctx->workspace)
                ctx->workspace->undoStack().clear();
            if (ctx->pane)
                ctx->pane->undoStack().clear();
            ctx->isProjectDirty = false;
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
    } catch (const std::exception& ex) {
        std::cerr << "[FluidCore] Exception in performSaveProject: " << ex.what() << std::endl;
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed", ex.what());
    } catch (...) {
        std::cerr << "[FluidCore] Unknown exception in performSaveProject" << std::endl;
        if (ctx->headerBar)
            ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Failed);
        showMessage(ctx->window, GTK_MESSAGE_ERROR, "Save Failed",
                    "An unexpected error occurred while saving.");
    }
}

void performOpenProject(AppViewContext* ctx) {
    if (!ctx)
        return;

    confirmDiscardUnsavedChanges(ctx, [ctx]() {
        try {
            GtkWidget* dialog = gtk_file_chooser_dialog_new(
                "Open FluidCore Project (.ltproj Bundle)", ctx->window,
                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL, "_Open",
                GTK_RESPONSE_ACCEPT, nullptr);

            if (ctx->pane && !ctx->pane->pdfPath().empty()) {
                std::error_code ec;
                std::filesystem::path curDir =
                    std::filesystem::path(ctx->pane->pdfPath()).parent_path();
                if (std::filesystem::exists(curDir, ec)) {
                    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                                        curDir.string().c_str());
                }
            }

            gint res = gtk_dialog_run(GTK_DIALOG(dialog));
            if (res != GTK_RESPONSE_ACCEPT) {
                gtk_widget_destroy(dialog);
                return;
            }

            gchar* rawChosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            gtk_widget_destroy(dialog);
            if (!rawChosen)
                return;

            std::string chosenPath = normalizePath(rawChosen);
            g_free(rawChosen);

            chosenPath = resolveLtprojPath(chosenPath);

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

            if (ctx->excerptTileCache) {
                ctx->excerptTileCache->clear();
            }

            auto docs = ctx->engine->projectStore().listDocuments();
            // Filter to find actual PDF documents (ignore image asset records)
            std::vector<FluidCore::DocumentRecord> pdfDocs;
            for (const auto& doc : docs) {
                std::string fn = doc.filename;
                std::string rel = doc.relativePath;
                std::string ext = doc.externalPath;
                auto endsWithPdf = [](const std::string& s) {
                    if (s.size() < 4)
                        return false;
                    std::string tail = s.substr(s.size() - 4);
                    std::transform(tail.begin(), tail.end(), tail.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    return tail == ".pdf";
                };
                if (endsWithPdf(fn) || endsWithPdf(rel) || endsWithPdf(ext) || !ext.empty()) {
                    pdfDocs.push_back(doc);
                }
            }

            if (!pdfDocs.empty()) {
                std::filesystem::path bundle(chosenPath);
                auto primaryDoc = pdfDocs[0];
                std::string resolvedPath;
                std::error_code ec;

                // Option 1: Legacy bundled document
                std::filesystem::path bundledPath = bundle / primaryDoc.relativePath;
                if (std::filesystem::exists(bundledPath, ec) &&
                    std::filesystem::is_regular_file(bundledPath, ec)) {
                    resolvedPath = bundledPath.string();
                }
                ec.clear();

                // Option 2: External path reference
                if (resolvedPath.empty() && !primaryDoc.externalPath.empty()) {
                    std::filesystem::path extPath(primaryDoc.externalPath);
                    if (std::filesystem::exists(extPath, ec) &&
                        std::filesystem::is_regular_file(extPath, ec)) {
                        resolvedPath = extPath.string();
                    }
                    ec.clear();
                }

                // Option 3: Fallback check relativePath as literal path
                if (resolvedPath.empty() && !primaryDoc.relativePath.empty()) {
                    std::filesystem::path literalPath(primaryDoc.relativePath);
                    if (std::filesystem::exists(literalPath, ec) &&
                        std::filesystem::is_regular_file(literalPath, ec)) {
                        resolvedPath = literalPath.string();
                    }
                    ec.clear();
                }

                // Missing / Moved external PDF: Prompt interactive "Locate PDF" flow
                if (resolvedPath.empty()) {
                    std::string missingLoc = !primaryDoc.externalPath.empty()
                                                 ? primaryDoc.externalPath
                                                 : bundledPath.string();
                    GtkWidget* warnDialog = gtk_message_dialog_new(
                        ctx->window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                        "Referenced PDF document could not be found at:\n%s\n\nWould you like to "
                        "locate the replacement PDF file?",
                        missingLoc.c_str());
                    gtk_dialog_add_button(GTK_DIALOG(warnDialog), "_Cancel", GTK_RESPONSE_CANCEL);
                    gtk_dialog_add_button(GTK_DIALOG(warnDialog), "_Locate PDF...",
                                          GTK_RESPONSE_ACCEPT);
                    gint resp = gtk_dialog_run(GTK_DIALOG(warnDialog));
                    gtk_widget_destroy(warnDialog);

                    if (resp == GTK_RESPONSE_ACCEPT) {
                        GtkFileChooserNative* native = gtk_file_chooser_native_new(
                            "Locate Replacement PDF Document", ctx->window,
                            GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
                        configureNativeFileChooser(native, ctx);

                        GtkFileFilter* filter = gtk_file_filter_new();
                        gtk_file_filter_set_name(filter, "PDF Documents (*.pdf)");
                        gtk_file_filter_add_pattern(filter, "*.pdf");
                        gtk_file_filter_add_mime_type(filter, "application/pdf");
                        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

                        gint fRes = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
                        if (fRes == GTK_RESPONSE_ACCEPT) {
                            gchar* rawLoc = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
                            if (rawLoc) {
                                resolvedPath = normalizePath(rawLoc);
                                g_free(rawLoc);
                                primaryDoc.externalPath =
                                    std::filesystem::absolute(resolvedPath, ec)
                                        .lexically_normal()
                                        .string();
                                ec.clear();
                                ctx->engine->projectStore().registerDocument(primaryDoc, nullptr);
                                ctx->isProjectDirty = true;
                            }
                        }
                        g_object_unref(native);
                    }
                }

                if (!resolvedPath.empty() && ctx->pane) {
                    size_t targetPage = 0;
                    if (primaryDoc.pageCount > 0 &&
                        primaryDoc.lastViewedPage < primaryDoc.pageCount) {
                        targetPage = primaryDoc.lastViewedPage;
                    } else if (primaryDoc.pageCount > 0) {
                        targetPage = primaryDoc.pageCount - 1;
                    }
                    ctx->pane->loadDocument(resolvedPath, primaryDoc.docId, targetPage);

                    // Load companion .xopp from bundle
                    std::string docStem =
                        std::filesystem::path(primaryDoc.filename).stem().string();
                    std::filesystem::path xopp1 = bundle / "documents" / (docStem + ".xopp");
                    std::filesystem::path xopp2 = bundle / primaryDoc.relativePath;
                    xopp2.replace_extension(".xopp");

                    std::string companionToUse = xopp1.string();
                    if (std::filesystem::exists(xopp1, ec)) {
                        companionToUse = xopp1.string();
                    } else if (std::filesystem::exists(xopp2, ec)) {
                        companionToUse = xopp2.string();
                    }
                    ec.clear();

                    ctx->pane->setCompanionPath(companionToUse);
                    ctx->pane->loadCompanionAnnotations(companionToUse);
                } else if (ctx->pane) {
                    ctx->pane->closeDocument();
                }

                if (ctx->pdfDocService && !resolvedPath.empty()) {
                    ctx->pdfDocService->clear();
                    PopplerDocument* activeDoc =
                        (ctx->pane && ctx->pane->document()) ? ctx->pane->document() : nullptr;
                    for (const auto& doc : docs) {
                        std::string dPath;
                        if (doc.docId == primaryDoc.docId) {
                            dPath = resolvedPath;
                        } else {
                            if (std::filesystem::exists(bundle / doc.relativePath, ec)) {
                                dPath = (bundle / doc.relativePath).string();
                            } else if (!doc.externalPath.empty()) {
                                dPath = doc.externalPath;
                            } else if (std::filesystem::exists(bundle / "documents" / doc.filename,
                                                               ec)) {
                                dPath = (bundle / "documents" / doc.filename).string();
                            }
                            ec.clear();
                            if (dPath.empty()) {
                                dPath = resolvedPath;
                            }
                        }
                        ctx->pdfDocService->registerMainDocument(doc.docId, activeDoc, dPath);
                        if (!doc.relativePath.empty() && doc.relativePath != doc.docId) {
                            ctx->pdfDocService->registerMainDocument(doc.relativePath, activeDoc,
                                                                     dPath);
                        }
                        if (!doc.filename.empty() && doc.filename != doc.docId) {
                            ctx->pdfDocService->registerMainDocument(doc.filename, activeDoc,
                                                                     dPath);
                        }
                    }
                }
            } else {
                if (ctx->pane) {
                    ctx->pane->closeDocument();
                }
            }

            if (ctx->excerptTileCache) {
                ctx->excerptTileCache->clear();
            }
            if (ctx->workspace) {
                ctx->workspace->undoStack().clear();
                ctx->workspace->setProjectBundlePath(chosenPath);
                ctx->workspace->notifyModelReloaded();
            }
            if (ctx->pane) {
                ctx->pane->undoStack().clear();
            }
            if (ctx->toolManager) {
                ctx->toolManager->setActiveTool(FluidCoreApp::Tool::Select);
            }
            ctx->isProjectDirty = false;
            if (ctx->headerBar) {
                ctx->headerBar->setProjectTitle(ctx->engine->projectTitle(), chosenPath);
                ctx->headerBar->setSaveStatus(FluidCoreApp::SaveStatus::Saved);
            }
            if (ctx->updateUndoRedoUI) {
                ctx->updateUndoRedoUI();
            }
        } catch (const std::exception& ex) {
            std::cerr << "[FluidCore] Exception in performOpenProject: " << ex.what() << std::endl;
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Error Opening Project", ex.what());
        } catch (...) {
            std::cerr << "[FluidCore] Unknown error in performOpenProject" << std::endl;
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Error Opening Project",
                        "An unexpected error occurred while opening the project.");
        }
    });
}

void performOpenPdf(AppViewContext* ctx) {
    if (!ctx)
        return;

    confirmDiscardUnsavedChanges(ctx, [ctx]() {
        try {
            GtkFileChooserNative* native = gtk_file_chooser_native_new(
                "Open PDF Document", ctx->window, GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
            configureNativeFileChooser(native, ctx);

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
                std::string previousPdf = ctx->pane->pdfPath();
                bool loaded = ctx->pane->loadDocument(chosenPath);
                if (loaded && ctx->pane->document() && ctx->pdfDocService) {
                    ctx->pdfDocService->registerMainDocument(ctx->pane->docId(),
                                                             ctx->pane->document(), chosenPath);
                    ctx->pdfDocService->registerMainDocument(chosenPath, ctx->pane->document(),
                                                             chosenPath);
                }
                if (ctx->excerptTileCache) {
                    if (!previousPdf.empty() && previousPdf == chosenPath) {
                        ctx->excerptTileCache->invalidate(chosenPath);
                    } else {
                        ctx->excerptTileCache->clear();
                    }
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
        } catch (const std::exception& ex) {
            std::cerr << "[FluidCore] Exception in performOpenPdf: " << ex.what() << std::endl;
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Error Opening PDF", ex.what());
        } catch (...) {
            std::cerr << "[FluidCore] Unknown error in performOpenPdf" << std::endl;
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Error Opening PDF",
                        "An unexpected error occurred while opening the PDF.");
        }
    });
}

void performNewProject(AppViewContext* ctx) {
    if (!ctx)
        return;

    confirmDiscardUnsavedChanges(ctx, [ctx]() {
        try {
            if (ctx->engine) {
                ctx->engine->newProject("Untitled Project");
            }
            if (ctx->workspace) {
                ctx->workspace->setProjectBundlePath("");
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
        } catch (const std::exception& ex) {
            std::cerr << "[FluidCore] Exception in performNewProject: " << ex.what() << std::endl;
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Error Creating Project", ex.what());
        } catch (...) {
            std::cerr << "[FluidCore] Unknown error in performNewProject" << std::endl;
            showMessage(ctx->window, GTK_MESSAGE_ERROR, "Error Creating Project",
                        "An unexpected error occurred.");
        }
    });
}

void performExport(AppViewContext* ctx) {
    if (!ctx)
        return;
    FluidCoreApp::ExportDialog::show(ctx->window, ctx->pane, ctx->workspace, ctx->engine);
}

void scheduleScenarioA(AppViewContext* ctx) {
    FluidCoreApp::MemoryTelemetry::log(
        "[Automated Protocol] Launching Scenario A: Search Scan without Navigation, followed by "
        "Search with Navigation, then Search Twice...");

    // Step 1: Post-Load Baseline (1500ms)
    g_timeout_add(
        1500,
        +[](gpointer data) -> gboolean {
            auto* vCtx = static_cast<AppViewContext*>(data);
            FluidCoreApp::MemoryTelemetry::log(
                "\n==================== [CHECKPOINT 1: POST-LOAD BASELINE] ====================");
            FluidCoreApp::MemoryTelemetry::log(
                "Process Private: " +
                FluidCoreApp::MemoryTelemetry::formatMB(
                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes()) +
                " | WS: " +
                FluidCoreApp::MemoryTelemetry::formatMB(
                    FluidCoreApp::MemoryTelemetry::getProcessWorkingSet()));
            vCtx->pane->pageTileCache().dumpStats("Checkpoint 1: Post-Load Baseline");
            vCtx->excerptTileCache->dumpStats("Checkpoint 1: Post-Load Baseline");
            FluidCoreApp::PopplerLifetimeTracker::dump("Checkpoint 1: Post-Load Baseline");
            FluidCoreApp::MemoryTelemetry::runHeapMin("Checkpoint 1: Post-Load Baseline");

            // Step 2: Navigate to Middle Page (Page 446) (next in 1000ms)
            g_timeout_add(
                1000,
                +[](gpointer d2) -> gboolean {
                    auto* v2 = static_cast<AppViewContext*>(d2);
                    FluidCoreApp::MemoryTelemetry::log(
                        "[Automated Protocol] Navigating to middle page (Page 446)...");
                    v2->pane->scrollToPage(446);

                    // Step 3: Checkpoint 2 (Middle Page Navigation) (1500ms later)
                    g_timeout_add(
                        1500,
                        +[](gpointer d3) -> gboolean {
                            auto* v3 = static_cast<AppViewContext*>(d3);
                            FluidCoreApp::MemoryTelemetry::log(
                                "\n==================== [CHECKPOINT 2: MIDDLE PAGE NAVIGATION] "
                                "====================");
                            FluidCoreApp::MemoryTelemetry::log(
                                "Process Private: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(
                                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes()) +
                                " | WS: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(
                                    FluidCoreApp::MemoryTelemetry::getProcessWorkingSet()));
                            v3->pane->pageTileCache().dumpStats("Checkpoint 2: Middle Page");
                            v3->excerptTileCache->dumpStats("Checkpoint 2: Middle Page");
                            FluidCoreApp::PopplerLifetimeTracker::dump("Checkpoint 2: Middle Page");

                            // Step 4: Search for "volatility" WITHOUT navigation (next in 1000ms)
                            g_timeout_add(
                                1000,
                                +[](gpointer d4) -> gboolean {
                                    auto* v4 = static_cast<AppViewContext*>(d4);
                                    FluidCoreApp::MemoryTelemetry::log(
                                        "[Automated Protocol] Starting search for 'volatility' (NO "
                                        "NAVIGATION)...");
                                    v4->pane->performSearch("volatility", false);

                                    // Step 5: Wait for search to finish across 892 pages (10
                                    // seconds)
                                    g_timeout_add(
                                        10000,
                                        +[](gpointer d5) -> gboolean {
                                            auto* v5 = static_cast<AppViewContext*>(d5);
                                            FluidCoreApp::MemoryTelemetry::log(
                                                "\n==================== [CHECKPOINT 3: SEARCH "
                                                "COMPLETED (NO NAVIGATION)] ====================");
                                            FluidCoreApp::MemoryTelemetry::log(
                                                "Search hits found: " +
                                                std::to_string(v5->pane->searchHits().size()) +
                                                " | Private: " +
                                                FluidCoreApp::MemoryTelemetry::formatMB(
                                                    FluidCoreApp::MemoryTelemetry::
                                                        getProcessPrivateBytes()) +
                                                " | WS: " +
                                                FluidCoreApp::MemoryTelemetry::formatMB(
                                                    FluidCoreApp::MemoryTelemetry::
                                                        getProcessWorkingSet()));
                                            v5->pane->pageTileCache().dumpStats(
                                                "Checkpoint 3 (Search Complete, No Nav)");
                                            v5->excerptTileCache->dumpStats(
                                                "Checkpoint 3 (Search Complete, No Nav)");
                                            FluidCoreApp::PopplerLifetimeTracker::dump(
                                                "Checkpoint 3 (Search Complete, No Nav)");
                                            FluidCoreApp::MemoryTelemetry::runHeapMin(
                                                "Checkpoint 3: Search Completed (No Nav)");

                                            // Step 6: Close Find (Escape) (next in 1000ms)
                                            g_timeout_add(
                                                1000,
                                                +[](gpointer d6) -> gboolean {
                                                    auto* v6 = static_cast<AppViewContext*>(d6);
                                                    FluidCoreApp::MemoryTelemetry::log(
                                                        "[Automated Protocol] Closing Find bar "
                                                        "(closeSearch)...");
                                                    v6->pane->closeSearch();

                                                    // Step 7: Checkpoint 4 (After Closing Find)
                                                    // (1500ms later)
                                                    g_timeout_add(
                                                        1500,
                                                        +[](gpointer d7) -> gboolean {
                                                            auto* v7 =
                                                                static_cast<AppViewContext*>(d7);
                                                            FluidCoreApp::MemoryTelemetry::log(
                                                                "\n==================== "
                                                                "[CHECKPOINT 4: AFTER CLOSING FIND "
                                                                "BAR] ====================");
                                                            FluidCoreApp::MemoryTelemetry::log(
                                                                "Process Private: " +
                                                                FluidCoreApp::MemoryTelemetry::formatMB(
                                                                    FluidCoreApp::MemoryTelemetry::
                                                                        getProcessPrivateBytes()) +
                                                                " | WS: " +
                                                                FluidCoreApp::MemoryTelemetry::
                                                                    formatMB(
                                                                        FluidCoreApp::MemoryTelemetry::
                                                                            getProcessWorkingSet()));
                                                            v7->pane->pageTileCache().dumpStats(
                                                                "Checkpoint 4 (After Close)");
                                                            v7->excerptTileCache->dumpStats(
                                                                "Checkpoint 4 (After Close)");
                                                            FluidCoreApp::PopplerLifetimeTracker::
                                                                dump("Checkpoint 4 (After Close)");

                                                            // Step 8: (Experiment 2) Search WITH
                                                            // Navigation (next in 1000ms)
                                                            g_timeout_add(
                                                                1000,
                                                                +[](gpointer d8) -> gboolean {
                                                                    auto* v8 = static_cast<
                                                                        AppViewContext*>(d8);
                                                                    FluidCoreApp::MemoryTelemetry::
                                                                        log("\n[Automated "
                                                                            "Protocol] Starting "
                                                                            "Experiment 2: Search "
                                                                            "WITH Navigation "
                                                                            "(jumping 15 "
                                                                            "distributed hits at "
                                                                            "250ms cadence)...");
                                                                    v8->pane->performSearch(
                                                                        "volatility", false);

                                                                    // Wait for search then navigate
                                                                    g_timeout_add(
                                                                        8000,
                                                                        +[](gpointer d9)
                                                                            -> gboolean {
                                                                            auto* v9 = static_cast<
                                                                                AppViewContext*>(
                                                                                d9);
                                                                            const auto& hits =
                                                                                v9->pane
                                                                                    ->searchHits();
                                                                            std::vector<std::size_t>
                                                                                targetHitIndices;
                                                                            if (!hits.empty()) {
                                                                                const std::size_t
                                                                                    totalHits =
                                                                                        hits.size();
                                                                                const int numSteps =
                                                                                    15;
                                                                                for (int s = 0;
                                                                                     s < numSteps;
                                                                                     ++s) {
                                                                                    std::size_t idx =
                                                                                        (totalHits >
                                                                                         1)
                                                                                            ? (static_cast<
                                                                                                   std::
                                                                                                       size_t>(
                                                                                                   s) *
                                                                                               (totalHits -
                                                                                                1)) /
                                                                                                  (numSteps -
                                                                                                   1)
                                                                                            : 0;
                                                                                    targetHitIndices
                                                                                        .push_back(
                                                                                            idx);
                                                                                }
                                                                            }

                                                                            struct NavContext {
                                                                                AppViewContext* ctx;
                                                                                std::vector<
                                                                                    std::size_t>
                                                                                    indices;
                                                                                std::size_t
                                                                                    current = 0;
                                                                            };

                                                                            auto* navCtx = new NavContext{
                                                                                v9,
                                                                                std::move(
                                                                                    targetHitIndices),
                                                                                0};

                                                                            g_timeout_add(
                                                                                250,
                                                                                +[](gpointer data)
                                                                                    -> gboolean {
                                                                                    auto* nc = static_cast<
                                                                                        NavContext*>(
                                                                                        data);
                                                                                    if (nc->current <
                                                                                        nc->indices
                                                                                            .size()) {
                                                                                        const std::size_t
                                                                                            hitIdx =
                                                                                                nc->indices
                                                                                                    [nc->current];
                                                                                        nc->ctx
                                                                                            ->pane
                                                                                            ->scrollToSearchHit(
                                                                                                hitIdx);
                                                                                        if (nc->ctx
                                                                                                ->pane
                                                                                                ->widget()) {
                                                                                            gtk_widget_queue_draw(
                                                                                                nc->ctx
                                                                                                    ->pane
                                                                                                    ->widget());
                                                                                        }
                                                                                        nc->current++;
                                                                                        if (nc->current ==
                                                                                            5) {
                                                                                            FluidCoreApp::MemoryTelemetry::runHeapMin(
                                                                                                "Na"
                                                                                                "vi"
                                                                                                "ga"
                                                                                                "ti"
                                                                                                "on"
                                                                                                " S"
                                                                                                "te"
                                                                                                "p "
                                                                                                "5 "
                                                                                                "(A"
                                                                                                "ft"
                                                                                                "er"
                                                                                                " 5"
                                                                                                " N"
                                                                                                "av"
                                                                                                "ig"
                                                                                                "at"
                                                                                                "io"
                                                                                                "ns"
                                                                                                ")");
                                                                                        } else if (
                                                                                            nc->current ==
                                                                                            10) {
                                                                                            FluidCoreApp::MemoryTelemetry::runHeapMin(
                                                                                                "Na"
                                                                                                "vi"
                                                                                                "ga"
                                                                                                "ti"
                                                                                                "on"
                                                                                                " S"
                                                                                                "te"
                                                                                                "p "
                                                                                                "10"
                                                                                                " ("
                                                                                                "Af"
                                                                                                "te"
                                                                                                "r "
                                                                                                "10"
                                                                                                " N"
                                                                                                "av"
                                                                                                "ig"
                                                                                                "at"
                                                                                                "io"
                                                                                                "ns"
                                                                                                ")");
                                                                                        }
                                                                                        return G_SOURCE_CONTINUE;
                                                                                    }

                                                                                    // Finished all
                                                                                    // 15 navigation
                                                                                    // steps
                                                                                    auto* v10 =
                                                                                        nc->ctx;
                                                                                    delete nc;

                                                                                    // Checkpoint 5
                                                                                    // (Search With
                                                                                    // Nav) (1000ms
                                                                                    // after last
                                                                                    // navigation
                                                                                    // step)
                                                                                    g_timeout_add(
                                                                                        1000,
                                                                                        +[](gpointer
                                                                                                d10)
                                                                                            -> gboolean {
                                                                                            auto* vContext = static_cast<
                                                                                                AppViewContext*>(
                                                                                                d10);
                                                                                            FluidCoreApp::MemoryTelemetry::log(
                                                                                                "\n"
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                " ["
                                                                                                "CH"
                                                                                                "EC"
                                                                                                "KP"
                                                                                                "OI"
                                                                                                "NT"
                                                                                                " 5"
                                                                                                ": "
                                                                                                "SE"
                                                                                                "AR"
                                                                                                "CH"
                                                                                                " W"
                                                                                                "IT"
                                                                                                "H "
                                                                                                "NA"
                                                                                                "VI"
                                                                                                "GA"
                                                                                                "TI"
                                                                                                "ON"
                                                                                                " ("
                                                                                                "15"
                                                                                                " D"
                                                                                                "IS"
                                                                                                "TR"
                                                                                                "IB"
                                                                                                "UT"
                                                                                                "ED"
                                                                                                " H"
                                                                                                "IT"
                                                                                                "S)"
                                                                                                "] "
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "=="
                                                                                                "="
                                                                                                "=");
                                                                                            FluidCoreApp::MemoryTelemetry::log(
                                                                                                "Pr"
                                                                                                "oc"
                                                                                                "es"
                                                                                                "s "
                                                                                                "Pr"
                                                                                                "iv"
                                                                                                "at"
                                                                                                "e:"
                                                                                                " " +
                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                    formatMB(
                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                            getProcessPrivateBytes()) +
                                                                                                " |"
                                                                                                " W"
                                                                                                "S:"
                                                                                                " " +
                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                    formatMB(
                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                            getProcessWorkingSet()));
                                                                                            vContext
                                                                                                ->pane
                                                                                                ->pageTileCache()
                                                                                                .dumpStats(
                                                                                                    "Checkpoint 5 (With Nav)");
                                                                                            vContext
                                                                                                ->excerptTileCache
                                                                                                ->dumpStats(
                                                                                                    "Checkpoint 5 (With Nav)");
                                                                                            FluidCoreApp::PopplerLifetimeTracker::
                                                                                                dump(
                                                                                                    "Ch"
                                                                                                    "ec"
                                                                                                    "kp"
                                                                                                    "oi"
                                                                                                    "nt"
                                                                                                    " 5"
                                                                                                    " ("
                                                                                                    "Wi"
                                                                                                    "th"
                                                                                                    " N"
                                                                                                    "av"
                                                                                                    ")");
                                                                                            FluidCoreApp::MemoryTelemetry::runHeapMin(
                                                                                                "Ch"
                                                                                                "ec"
                                                                                                "kp"
                                                                                                "oi"
                                                                                                "nt"
                                                                                                " 5"
                                                                                                ": "
                                                                                                "Af"
                                                                                                "te"
                                                                                                "r "
                                                                                                "15"
                                                                                                " N"
                                                                                                "av"
                                                                                                "ig"
                                                                                                "at"
                                                                                                "io"
                                                                                                "n"
                                                                                                "s");

                                                                                            // Close
                                                                                            // Find
                                                                                            vContext
                                                                                                ->pane
                                                                                                ->closeSearch();

                                                                                            // Checkpoint
                                                                                            // 6
                                                                                            // (After
                                                                                            // Closing
                                                                                            // Post-Nav)
                                                                                            // (1500ms
                                                                                            // later)
                                                                                            g_timeout_add(
                                                                                                1500,
                                                                                                +[](gpointer
                                                                                                        d11)
                                                                                                    -> gboolean {
                                                                                                    auto* v11 = static_cast<
                                                                                                        AppViewContext*>(
                                                                                                        d11);
                                                                                                    FluidCoreApp::MemoryTelemetry::
                                                                                                        log("\n==================== [CHECKPOINT 6: AFTER CLOSING FIND (POST-NAV)] ====================");
                                                                                                    FluidCoreApp::MemoryTelemetry::log(
                                                                                                        "Process Private: " +
                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                            formatMB(
                                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                                    getProcessPrivateBytes()) +
                                                                                                        " | WS: " +
                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                            formatMB(
                                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                                    getProcessWorkingSet()));
                                                                                                    v11->pane
                                                                                                        ->pageTileCache()
                                                                                                        .dumpStats(
                                                                                                            "Checkpoint 6");
                                                                                                    v11->excerptTileCache
                                                                                                        ->dumpStats(
                                                                                                            "Checkpoint 6");
                                                                                                    FluidCoreApp::PopplerLifetimeTracker::
                                                                                                        dump(
                                                                                                            "Checkpoint 6");

                                                                                                    // Step 12: (Experiment 3) Search Twice Accumulation ("futures")
                                                                                                    g_timeout_add(
                                                                                                        1000,
                                                                                                        +[](gpointer
                                                                                                                d12)
                                                                                                            -> gboolean {
                                                                                                            auto* v12 = static_cast<
                                                                                                                AppViewContext*>(
                                                                                                                d12);
                                                                                                            FluidCoreApp::MemoryTelemetry::
                                                                                                                log("\n[Automated Protocol] Starting Experiment 3: Search Twice (Query: 'futures')...");
                                                                                                            v12->pane
                                                                                                                ->performSearch(
                                                                                                                    "futures",
                                                                                                                    false);

                                                                                                            // Wait 10s for search #2
                                                                                                            g_timeout_add(
                                                                                                                10000,
                                                                                                                +[](gpointer
                                                                                                                        d13)
                                                                                                                    -> gboolean {
                                                                                                                    auto* v13 = static_cast<
                                                                                                                        AppViewContext*>(
                                                                                                                        d13);
                                                                                                                    FluidCoreApp::MemoryTelemetry::
                                                                                                                        log("\n==================== [CHECKPOINT 7: SEARCH #2 COMPLETED (FUTURES)] ====================");
                                                                                                                    FluidCoreApp::MemoryTelemetry::log(
                                                                                                                        "Search hits found: " +
                                                                                                                        std::to_string(
                                                                                                                            v13->pane
                                                                                                                                ->searchHits()
                                                                                                                                .size()) +
                                                                                                                        " | Private: " +
                                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                                            formatMB(
                                                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                                                    getProcessPrivateBytes()) +
                                                                                                                        " | WS: " +
                                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                                            formatMB(
                                                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                                                    getProcessWorkingSet()));
                                                                                                                    v13->pane
                                                                                                                        ->pageTileCache()
                                                                                                                        .dumpStats(
                                                                                                                            "Checkpoint 7");
                                                                                                                    v13->excerptTileCache
                                                                                                                        ->dumpStats(
                                                                                                                            "Checkpoint 7");
                                                                                                                    FluidCoreApp::PopplerLifetimeTracker::
                                                                                                                        dump(
                                                                                                                            "Checkpoint 7");

                                                                                                                    // Close Find #2
                                                                                                                    v13->pane
                                                                                                                        ->closeSearch();

                                                                                                                    // Checkpoint 8 (After Close #2)
                                                                                                                    g_timeout_add(
                                                                                                                        1500,
                                                                                                                        +[](gpointer
                                                                                                                                d14)
                                                                                                                            -> gboolean {
                                                                                                                            auto* v14 = static_cast<
                                                                                                                                AppViewContext*>(
                                                                                                                                d14);
                                                                                                                            FluidCoreApp::MemoryTelemetry::
                                                                                                                                log("\n==================== [CHECKPOINT 8: AFTER CLOSING FIND #2] ====================");
                                                                                                                            FluidCoreApp::MemoryTelemetry::log(
                                                                                                                                "Process Private: " +
                                                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                                                    formatMB(
                                                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                                                            getProcessPrivateBytes()) +
                                                                                                                                " | WS: " +
                                                                                                                                FluidCoreApp::MemoryTelemetry::
                                                                                                                                    formatMB(
                                                                                                                                        FluidCoreApp::MemoryTelemetry::
                                                                                                                                            getProcessWorkingSet()));
                                                                                                                            v14->pane
                                                                                                                                ->pageTileCache()
                                                                                                                                .dumpStats(
                                                                                                                                    "Checkpoint 8");
                                                                                                                            v14->excerptTileCache
                                                                                                                                ->dumpStats(
                                                                                                                                    "Checkpoint 8");
                                                                                                                            FluidCoreApp::PopplerLifetimeTracker::
                                                                                                                                dump(
                                                                                                                                    "Checkpoint 8");
                                                                                                                            FluidCoreApp::MemoryTelemetry::
                                                                                                                                runHeapMin(
                                                                                                                                    "Checkpoint 8: Before Document Close");

                                                                                                                            FluidCoreApp::MemoryTelemetry::
                                                                                                                                log("\n[Automated Protocol] === SCENARIO A EXECUTION COMPLETE ===\n");
                                                                                                                            if (v14->pane) {
                                                                                                                                v14->pane
                                                                                                                                    ->closeDocument();
                                                                                                                            }
                                                                                                                            FluidCoreApp::MemoryTelemetry::
                                                                                                                                runHeapMin(
                                                                                                                                    "Teardown: After Document Close");
                                                                                                                            if (v14->app) {
                                                                                                                                g_application_quit(
                                                                                                                                    G_APPLICATION(
                                                                                                                                        v14->app));
                                                                                                                            }
                                                                                                                            return G_SOURCE_REMOVE;
                                                                                                                        },
                                                                                                                        v13);
                                                                                                                    return G_SOURCE_REMOVE;
                                                                                                                },
                                                                                                                v12);
                                                                                                            return G_SOURCE_REMOVE;
                                                                                                        },
                                                                                                        v11);
                                                                                                    return G_SOURCE_REMOVE;
                                                                                                },
                                                                                                vContext);
                                                                                            return G_SOURCE_REMOVE;
                                                                                        },
                                                                                        v10);
                                                                                    return G_SOURCE_REMOVE;
                                                                                },
                                                                                navCtx);
                                                                            return G_SOURCE_REMOVE;
                                                                        },
                                                                        v8);
                                                                    return G_SOURCE_REMOVE;
                                                                },
                                                                v7);
                                                            return G_SOURCE_REMOVE;
                                                        },
                                                        v6);
                                                    return G_SOURCE_REMOVE;
                                                },
                                                v5);
                                            return G_SOURCE_REMOVE;
                                        },
                                        v4);
                                    return G_SOURCE_REMOVE;
                                },
                                v3);
                            return G_SOURCE_REMOVE;
                        },
                        v2);
                    return G_SOURCE_REMOVE;
                },
                vCtx);
            return G_SOURCE_REMOVE;
        },
        ctx);
}

void scheduleScenarioB(AppViewContext* ctx) {
    FluidCoreApp::MemoryTelemetry::log(
        "[Automated Protocol] Launching Scenario B: Direct Navigation ONLY (NO SEARCH)...");

    // Step 1: Post-Load Baseline (1500ms)
    g_timeout_add(
        1500,
        +[](gpointer data) -> gboolean {
            auto* vCtx = static_cast<AppViewContext*>(data);
            FluidCoreApp::MemoryTelemetry::log("\n==================== [SCENARIO B - CHECKPOINT 1: "
                                               "POST-LOAD BASELINE] ====================");
            FluidCoreApp::MemoryTelemetry::log(
                "Process Private: " +
                FluidCoreApp::MemoryTelemetry::formatMB(
                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes()) +
                " | WS: " +
                FluidCoreApp::MemoryTelemetry::formatMB(
                    FluidCoreApp::MemoryTelemetry::getProcessWorkingSet()));
            vCtx->pane->pageTileCache().dumpStats("Scenario B Baseline");
            vCtx->excerptTileCache->dumpStats("Scenario B Baseline");
            FluidCoreApp::PopplerLifetimeTracker::dump("Scenario B Baseline");

            // Step 2: Navigate to EXACT SAME Middle Page (Page 446) (next in 1000ms)
            g_timeout_add(
                1000,
                +[](gpointer d2) -> gboolean {
                    auto* v2 = static_cast<AppViewContext*>(d2);
                    FluidCoreApp::MemoryTelemetry::log(
                        "[Automated Protocol] Navigating directly to Page 446 (NO SEARCH)...");
                    v2->pane->scrollToPage(446);

                    // Step 3: Checkpoint 2 (Middle Page Navigation Only) (2500ms later)
                    g_timeout_add(
                        2500,
                        +[](gpointer d3) -> gboolean {
                            auto* v3 = static_cast<AppViewContext*>(d3);
                            FluidCoreApp::MemoryTelemetry::log(
                                "\n==================== [SCENARIO B - CHECKPOINT 2: MIDDLE PAGE "
                                "NAVIGATION ONLY] ====================");
                            FluidCoreApp::MemoryTelemetry::log(
                                "Process Private: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(
                                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes()) +
                                " | WS: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(
                                    FluidCoreApp::MemoryTelemetry::getProcessWorkingSet()));
                            v3->pane->pageTileCache().dumpStats("Scenario B Nav Only");
                            v3->excerptTileCache->dumpStats("Scenario B Nav Only");
                            FluidCoreApp::PopplerLifetimeTracker::dump("Scenario B Nav Only");

                            FluidCoreApp::MemoryTelemetry::log(
                                "\n[Automated Protocol] === SCENARIO B EXECUTION COMPLETE ===\n");
                            if (v3->app) {
                                g_application_quit(G_APPLICATION(v3->app));
                            }
                            return G_SOURCE_REMOVE;
                        },
                        v2);
                    return G_SOURCE_REMOVE;
                },
                vCtx);
            return G_SOURCE_REMOVE;
        },
        ctx);
}

void scheduleScenarioRepeatedFind(AppViewContext* ctx, int totalIterations = 20) {
    FluidCoreApp::MemoryTelemetry::log("[Automated Protocol] Launching Repeated Find Scenario (" +
                                       std::to_string(totalIterations) +
                                       "x consecutive searches for 'futures', NO NAVIGATION)...");

    struct FindTestRecord {
        int iteration = 0;
        std::size_t beforePriv = 0;
        std::size_t afterPriv = 0;
        long long deltaPriv = 0;
        std::size_t beforeLiveAlloc = 0;
        std::size_t afterLiveAlloc = 0;
        long long deltaLiveAlloc = 0;
        std::size_t beforeHeapCommit = 0;
        std::size_t afterHeapCommit = 0;
        std::size_t beforeMiCommit = 0;
        std::size_t afterMiCommit = 0;
        std::size_t livePages = 0;
        std::size_t totalCreated = 0;
        std::size_t totalDestroyed = 0;
        std::size_t ptcBytes = 0;
        std::size_t etcBytes = 0;
        std::size_t hitCount = 0;
    };

    struct TestState {
        AppViewContext* ctx = nullptr;
        int currentIteration = 1;
        int totalIterations = 20;
        const std::string query = "futures";
        std::vector<FindTestRecord> records;
        FluidCoreApp::MemoryTelemetry::ProcessHeapMetrics initialBaselineMetrics;
    };

    auto* state = new TestState{ctx, 1, totalIterations, "futures", {}, {}};

    // Step 1: Allow UI to settle (1500ms), then record baseline
    g_timeout_add(
        1500,
        +[](gpointer data) -> gboolean {
            auto* st = static_cast<TestState*>(data);
            st->initialBaselineMetrics = FluidCoreApp::MemoryTelemetry::getHeapMetrics();

            FluidCoreApp::MemoryTelemetry::log("\n==================== [REPEATED FIND TEST: "
                                               "INITIAL BASELINE] ====================");
            FluidCoreApp::MemoryTelemetry::log(
                "Initial Private: " +
                FluidCoreApp::MemoryTelemetry::formatMB(st->initialBaselineMetrics.privateBytes) +
                " | Initial WS: " +
                FluidCoreApp::MemoryTelemetry::formatMB(st->initialBaselineMetrics.workingSet) +
                " | Initial LiveAlloc: " +
                FluidCoreApp::MemoryTelemetry::formatMB(st->initialBaselineMetrics.heapAllocated) +
                " | Initial HeapCommit: " +
                FluidCoreApp::MemoryTelemetry::formatMB(st->initialBaselineMetrics.heapCommitted) +
                " | Initial MiCommit: " +
                FluidCoreApp::MemoryTelemetry::formatMB(
                    st->initialBaselineMetrics.mimallocCommitted));
            st->ctx->pane->pageTileCache().dumpStats("Initial Baseline");
            st->ctx->excerptTileCache->dumpStats("Initial Baseline");
            FluidCoreApp::PopplerLifetimeTracker::dump("Initial Baseline");

            struct Runner {
                static void step(TestState* s) {
                    if (s->currentIteration > s->totalIterations) {
                        // All iterations complete!
                        FluidCoreApp::MemoryTelemetry::log(
                            "\n==================== [REPEATED FIND TEST: FINAL REPORT (" +
                            std::to_string(s->totalIterations) + " PASSES)] ====================");
                        FluidCoreApp::MemoryTelemetry::log(
                            "Query: \"" + s->query + "\" across " +
                            std::to_string(s->ctx->pane->pages().size()) +
                            " pages (NO NAVIGATION)\n");

                        FluidCoreApp::MemoryTelemetry::log(
                            "| Find # | Before Priv | After Priv | Delta Priv | Before LiveAlloc | "
                            "After LiveAlloc | Delta LiveAlloc | HeapCommit | MiCommit | Hits |");
                        FluidCoreApp::MemoryTelemetry::log(
                            "|:------:|:-----------:|:----------:|:----------:|:----------------:|:"
                            "---------------:|:---------------:|:----------:|:--------:|:----:|");

                        for (const auto& r : s->records) {
                            std::string signP = r.deltaPriv >= 0 ? "+" : "-";
                            std::size_t absDP = r.deltaPriv >= 0 ? r.deltaPriv : -r.deltaPriv;
                            std::string signL = r.deltaLiveAlloc >= 0 ? "+" : "-";
                            std::size_t absDL =
                                r.deltaLiveAlloc >= 0 ? r.deltaLiveAlloc : -r.deltaLiveAlloc;

                            FluidCoreApp::MemoryTelemetry::log(
                                "| Find " + std::to_string(r.iteration) + " | " +
                                FluidCoreApp::MemoryTelemetry::formatMB(r.beforePriv) + " | " +
                                FluidCoreApp::MemoryTelemetry::formatMB(r.afterPriv) + " | " +
                                signP + FluidCoreApp::MemoryTelemetry::formatMB(absDP) + " | " +
                                FluidCoreApp::MemoryTelemetry::formatMB(r.beforeLiveAlloc) + " | " +
                                FluidCoreApp::MemoryTelemetry::formatMB(r.afterLiveAlloc) + " | " +
                                signL + FluidCoreApp::MemoryTelemetry::formatMB(absDL) + " | " +
                                FluidCoreApp::MemoryTelemetry::formatMB(r.afterHeapCommit) + " | " +
                                FluidCoreApp::MemoryTelemetry::formatMB(r.afterMiCommit) + " | " +
                                std::to_string(r.hitCount) + " |");
                        }

                        // CI Regression Gate: verify average LiveAlloc growth per pass
                        long long totalDeltaLiveAlloc = 0;
                        for (const auto& r : s->records) {
                            totalDeltaLiveAlloc += r.deltaLiveAlloc;
                        }
                        double avgDeltaLiveMB =
                            s->records.empty()
                                ? 0.0
                                : ((double)totalDeltaLiveAlloc / (double)s->records.size()) /
                                      (1024.0 * 1024.0);

                        double maxAllowedMB = 0.5; // Default 0.5 MB threshold per pass
                        if (const char* envThresh = g_getenv("FLUIDCORE_MAX_LIVEALLOC_GROWTH_MB")) {
                            double v = std::atof(envThresh);
                            if (v > 0.0)
                                maxAllowedMB = v;
                        }

                        bool gatePassed = (avgDeltaLiveMB <= maxAllowedMB);
                        FluidCoreApp::MemoryTelemetry::log(
                            "\n==================== [CI MEMORY REGRESSION GATE RESULT] "
                            "====================");
                        FluidCoreApp::MemoryTelemetry::log("Total Passes:              " +
                                                           std::to_string(s->records.size()));
                        FluidCoreApp::MemoryTelemetry::log(
                            "Cumulative LiveAlloc Net:  " +
                            FluidCoreApp::MemoryTelemetry::formatSignedMB(totalDeltaLiveAlloc));
                        FluidCoreApp::MemoryTelemetry::log(
                            std::string("Avg LiveAlloc per Pass:    ") +
                            (avgDeltaLiveMB >= 0 ? "+" : "") + std::to_string(avgDeltaLiveMB) +
                            " MB");
                        FluidCoreApp::MemoryTelemetry::log(
                            "Max Allowed Threshold:     " + std::to_string(maxAllowedMB) +
                            " MB/pass");
                        if (gatePassed) {
                            FluidCoreApp::MemoryTelemetry::log(
                                "Regression Gate Verdict:   PASS (0.00 MB leak; within strict CI "
                                "limit)\n");
                        } else {
                            FluidCoreApp::MemoryTelemetry::log(
                                "Regression Gate Verdict:   FAIL - LEAK DETECTED EXCEEDING " +
                                std::to_string(maxAllowedMB) + " MB/PASS!\n");
                        }
                        FluidCoreApp::MemoryTelemetry::log("======================================="
                                                           "=====================================");

                        if (!gatePassed) {
                            std::cerr << "[CI REGRESSION GATE FAILED] Average LiveAlloc growth ("
                                      << avgDeltaLiveMB << " MB/pass) exceeded threshold ("
                                      << maxAllowedMB << " MB/pass)!" << std::endl;
                            delete s;
                            std::exit(2);
                        }

                        FluidCoreApp::MemoryTelemetry::log(
                            "\n[Automated Protocol] Closing document and terminating...");
                        if (s->ctx->pane) {
                            s->ctx->pane->closeDocument();
                        }
                        const auto finalMetrics = FluidCoreApp::MemoryTelemetry::getHeapMetrics();
                        FluidCoreApp::MemoryTelemetry::log(
                            "[Teardown] Document closed. Final Private Bytes: " +
                            FluidCoreApp::MemoryTelemetry::formatMB(finalMetrics.privateBytes) +
                            " | Final LiveAlloc: " +
                            FluidCoreApp::MemoryTelemetry::formatMB(finalMetrics.heapAllocated) +
                            " (Initial Baseline Priv: " +
                            FluidCoreApp::MemoryTelemetry::formatMB(
                                s->initialBaselineMetrics.privateBytes) +
                            " | Initial Baseline LiveAlloc: " +
                            FluidCoreApp::MemoryTelemetry::formatMB(
                                s->initialBaselineMetrics.heapAllocated) +
                            ")");
                        GtkApplication* app = s->ctx->app;
                        delete s;
                        if (app) {
                            g_application_quit(G_APPLICATION(app));
                        }
                        std::exit(0);
                    }

                    const int iter = s->currentIteration;
                    const auto beforeM = FluidCoreApp::MemoryTelemetry::getHeapMetrics();

                    FluidCoreApp::MemoryTelemetry::log(
                        "\n------------------------------------------------------------");
                    FluidCoreApp::MemoryTelemetry::log("[Repeated Find] >>> STARTING FIND #" +
                                                       std::to_string(iter) + " (Query: \"" +
                                                       s->query + "\", autoNavigate: false) <<<");
                    FluidCoreApp::MemoryTelemetry::log(
                        "[Repeated Find] Before: Priv: " +
                        FluidCoreApp::MemoryTelemetry::formatMB(beforeM.privateBytes) +
                        " | LiveAlloc: " +
                        FluidCoreApp::MemoryTelemetry::formatMB(beforeM.heapAllocated) +
                        " | HeapCommit: " +
                        FluidCoreApp::MemoryTelemetry::formatMB(beforeM.heapCommitted));

                    s->ctx->pane->performSearch(
                        s->query,
                        /*enableSqueeze=*/false,
                        /*autoNavigate=*/false, [s, iter, beforeM]() {
                            const auto afterM = FluidCoreApp::MemoryTelemetry::getHeapMetrics();
                            const long long deltaP = static_cast<long long>(afterM.privateBytes) -
                                                     static_cast<long long>(beforeM.privateBytes);
                            const long long deltaL = static_cast<long long>(afterM.heapAllocated) -
                                                     static_cast<long long>(beforeM.heapAllocated);

                            FindTestRecord rec;
                            rec.iteration = iter;
                            rec.beforePriv = beforeM.privateBytes;
                            rec.afterPriv = afterM.privateBytes;
                            rec.deltaPriv = deltaP;
                            rec.beforeLiveAlloc = beforeM.heapAllocated;
                            rec.afterLiveAlloc = afterM.heapAllocated;
                            rec.deltaLiveAlloc = deltaL;
                            rec.beforeHeapCommit = beforeM.heapCommitted;
                            rec.afterHeapCommit = afterM.heapCommitted;
                            rec.beforeMiCommit = beforeM.mimallocCommitted;
                            rec.afterMiCommit = afterM.mimallocCommitted;
                            rec.livePages = FluidCoreApp::PopplerLifetimeTracker::getLivePages();
                            rec.totalCreated =
                                FluidCoreApp::PopplerLifetimeTracker::getTotalCreated();
                            rec.totalDestroyed =
                                FluidCoreApp::PopplerLifetimeTracker::getTotalDestroyed();
                            rec.ptcBytes = s->ctx->pane->pageTileCache().currentBytes();
                            rec.etcBytes = s->ctx->excerptTileCache
                                               ? s->ctx->excerptTileCache->currentBytes()
                                               : 0;
                            rec.hitCount = s->ctx->pane->searchHits().size();

                            s->records.push_back(rec);

                            std::string signP = deltaP >= 0 ? "+" : "-";
                            std::size_t absDP = deltaP >= 0 ? deltaP : -deltaP;
                            std::string signL = deltaL >= 0 ? "+" : "-";
                            std::size_t absDL = deltaL >= 0 ? deltaL : -deltaL;

                            FluidCoreApp::MemoryTelemetry::log(
                                "[Repeated Find] <<< COMPLETED FIND #" + std::to_string(iter) +
                                " >>>");
                            FluidCoreApp::MemoryTelemetry::log(
                                "    Private:   " +
                                FluidCoreApp::MemoryTelemetry::formatMB(beforeM.privateBytes) +
                                " -> " +
                                FluidCoreApp::MemoryTelemetry::formatMB(afterM.privateBytes) +
                                " (Delta: " + signP +
                                FluidCoreApp::MemoryTelemetry::formatMB(absDP) + ")");
                            FluidCoreApp::MemoryTelemetry::log(
                                "    LiveAlloc: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(beforeM.heapAllocated) +
                                " -> " +
                                FluidCoreApp::MemoryTelemetry::formatMB(afterM.heapAllocated) +
                                " (Delta: " + signL +
                                FluidCoreApp::MemoryTelemetry::formatMB(absDL) + ")");
                            FluidCoreApp::MemoryTelemetry::log(
                                "    HeapCommit:" +
                                FluidCoreApp::MemoryTelemetry::formatMB(afterM.heapCommitted) +
                                " | MiCommit: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(afterM.mimallocCommitted));
                            FluidCoreApp::MemoryTelemetry::log(
                                "    Hits: " + std::to_string(rec.hitCount) +
                                " | Poppler Live: " + std::to_string(rec.livePages));

                            // Now close search
                            FluidCoreApp::MemoryTelemetry::log(
                                "[Repeated Find] Invoking closeSearch()...");
                            s->ctx->pane->closeSearch();

                            const auto postCloseM = FluidCoreApp::MemoryTelemetry::getHeapMetrics();
                            FluidCoreApp::MemoryTelemetry::log(
                                "[Repeated Find] Post-closeSearch Priv: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(postCloseM.privateBytes) +
                                " | LiveAlloc: " +
                                FluidCoreApp::MemoryTelemetry::formatMB(postCloseM.heapAllocated));

                            // Advance iteration
                            s->currentIteration++;

                            // Wait 300ms before starting next search
                            g_timeout_add(
                                300,
                                +[](gpointer d) -> gboolean {
                                    auto* statePtr = static_cast<TestState*>(d);
                                    Runner::step(statePtr);
                                    return G_SOURCE_REMOVE;
                                },
                                s);
                        });
                }
            };

            Runner::step(st);
            return G_SOURCE_REMOVE;
        },
        state);
}

void scheduleScenarioReopenAudit(AppViewContext* ctx) {
    FluidCoreApp::MemoryTelemetry::log(
        "\n==================== [REOPEN & FIND_TEXT AUDIT PROTOCOL] ====================");

    struct AuditState {
        AppViewContext* ctx = nullptr;
        std::string pdfPath;
        std::size_t initialBaselinePriv = 0;
        std::size_t postClose1Priv = 0;
        std::size_t postReopenBaselinePriv = 0;
        std::size_t search1EndPriv = 0;
        std::size_t search2EndPriv = 0;
    };

    std::string currentPath = ctx->pane ? ctx->pane->pdfPath() : "";
    auto* state = new AuditState{ctx, currentPath, 0, 0, 0, 0, 0};

    // Step 1: Allow UI to settle (1500ms), record initial baseline
    g_timeout_add(
        1500,
        +[](gpointer data) -> gboolean {
            auto* s = static_cast<AuditState*>(data);
            s->initialBaselinePriv = FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
            FluidCoreApp::MemoryTelemetry::log(
                "[Audit Step 1] Initial Baseline Private: " +
                FluidCoreApp::MemoryTelemetry::formatMB(s->initialBaselinePriv));

            // Part A: Direct single-page findText loop on Page 6 (same PopplerPage* kept alive)
            PopplerDocument* doc = s->ctx->pane ? s->ctx->pane->document() : nullptr;
            if (doc) {
                PopplerPage* page6 = nullptr;
                {
                    std::lock_guard<std::mutex> lock(
                        FluidCoreApp::PdfDocumentService::globalPopplerMutex());
                    page6 = poppler_document_get_page(doc, 6);
                }
                if (page6) {
                    const std::size_t beforeFindLoop =
                        FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                    for (int iter = 1; iter <= 50; ++iter) {
                        GList* matches = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(
                                FluidCoreApp::PdfDocumentService::globalPopplerMutex());
                            matches = poppler_page_find_text_with_options(page6, "futures",
                                                                          POPPLER_FIND_DEFAULT);
                        }
                        for (GList* l = matches; l != nullptr; l = l->next) {
                            poppler_rectangle_free(static_cast<PopplerRectangle*>(l->data));
                        }
                        if (matches)
                            g_list_free(matches);

                        if (iter == 1 || iter == 10 || iter == 50) {
                            const std::size_t cur =
                                FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                            long long d = static_cast<long long>(cur) -
                                          static_cast<long long>(beforeFindLoop);
                            FluidCoreApp::MemoryTelemetry::log(
                                "    [Single Page FindText (Page 6)] Iter " + std::to_string(iter) +
                                "/50: " + FluidCoreApp::MemoryTelemetry::formatMB(cur) +
                                " (Delta from start: " + (d >= 0 ? "+" : "-") +
                                FluidCoreApp::MemoryTelemetry::formatMB(std::abs(d)) + ")");
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(
                            FluidCoreApp::PdfDocumentService::globalPopplerMutex());
                        g_object_unref(page6);
                    }
                    const std::size_t afterPage6Unref =
                        FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                    FluidCoreApp::MemoryTelemetry::log(
                        "    [Single Page FindText] Page 6 unref'd. Private: " +
                        FluidCoreApp::MemoryTelemetry::formatMB(afterPage6Unref));
                }

                // Part B: Ephemeral Page Get + FindText + Unref loop (50 iterations on Page 6)
                const std::size_t beforeEphemeralLoop =
                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                for (int iter = 1; iter <= 50; ++iter) {
                    PopplerPage* ephem = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(
                            FluidCoreApp::PdfDocumentService::globalPopplerMutex());
                        ephem = poppler_document_get_page(doc, 6);
                    }
                    if (ephem) {
                        GList* matches = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(
                                FluidCoreApp::PdfDocumentService::globalPopplerMutex());
                            matches = poppler_page_find_text_with_options(ephem, "futures",
                                                                          POPPLER_FIND_DEFAULT);
                        }
                        for (GList* l = matches; l != nullptr; l = l->next) {
                            poppler_rectangle_free(static_cast<PopplerRectangle*>(l->data));
                        }
                        if (matches)
                            g_list_free(matches);
                        {
                            std::lock_guard<std::mutex> lock(
                                FluidCoreApp::PdfDocumentService::globalPopplerMutex());
                            g_object_unref(ephem);
                        }
                    }
                    if (iter == 1 || iter == 10 || iter == 50) {
                        const std::size_t cur =
                            FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                        long long d = static_cast<long long>(cur) -
                                      static_cast<long long>(beforeEphemeralLoop);
                        FluidCoreApp::MemoryTelemetry::log(
                            "    [Ephemeral Get+Find+Unref (Page 6)] Iter " + std::to_string(iter) +
                            "/50: " + FluidCoreApp::MemoryTelemetry::formatMB(cur) +
                            " (Delta from start: " + (d >= 0 ? "+" : "-") +
                            FluidCoreApp::MemoryTelemetry::formatMB(std::abs(d)) + ")");
                    }
                }
            }

            // Part C: Run Full 892-Page Search 1
            FluidCoreApp::MemoryTelemetry::log("\n[Audit Step 2] Launching Full Document Search 1 "
                                               "(Query: 'futures', 892 pages)...");
            const std::size_t beforeSearch1 =
                FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
            s->ctx->pane->performSearch("futures", false, false, [s, beforeSearch1]() {
                s->search1EndPriv = FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                FluidCoreApp::MemoryTelemetry::log(
                    "[Audit Step 2] Search 1 Completed. Private: " +
                    FluidCoreApp::MemoryTelemetry::formatMB(beforeSearch1) + " -> " +
                    FluidCoreApp::MemoryTelemetry::formatMB(s->search1EndPriv) +
                    " (Hits: " + std::to_string(s->ctx->pane->searchHits().size()) + ")");

                s->ctx->pane->closeSearch();

                // Part D: Close the document!
                FluidCoreApp::MemoryTelemetry::log(
                    "\n[Audit Step 3] Closing document (closeDocument)...");
                s->ctx->pane->closeDocument();
                s->postClose1Priv = FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                FluidCoreApp::MemoryTelemetry::log(
                    "[Audit Step 3] Post-Close 1 Private Bytes: " +
                    FluidCoreApp::MemoryTelemetry::formatMB(s->postClose1Priv) +
                    " (Initial Baseline: " +
                    FluidCoreApp::MemoryTelemetry::formatMB(s->initialBaselinePriv) + ")");
                FluidCoreApp::MemoryTelemetry::runHeapMin("Post-Close 1 HeapMin");

                // Part E: Reopen the document! (Wait 1500ms then reload)
                g_timeout_add(
                    1500,
                    +[](gpointer d2) -> gboolean {
                        auto* s2 = static_cast<AuditState*>(d2);
                        FluidCoreApp::MemoryTelemetry::log(
                            "\n[Audit Step 4] Reopening document: " + s2->pdfPath + " ...");
                        s2->ctx->pane->loadDocument(s2->pdfPath);

                        // Wait 1500ms for load to settle
                        g_timeout_add(
                            1500,
                            +[](gpointer d3) -> gboolean {
                                auto* s3 = static_cast<AuditState*>(d3);
                                s3->postReopenBaselinePriv =
                                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                                FluidCoreApp::MemoryTelemetry::log(
                                    "\n==================== [AUDIT STEP 5: POST-REOPEN BASELINE] "
                                    "====================");
                                FluidCoreApp::MemoryTelemetry::log(
                                    "Initial Baseline (Run 1): " +
                                    FluidCoreApp::MemoryTelemetry::formatMB(
                                        s3->initialBaselinePriv));
                                FluidCoreApp::MemoryTelemetry::log(
                                    "Post-Close 1:            " +
                                    FluidCoreApp::MemoryTelemetry::formatMB(s3->postClose1Priv));
                                FluidCoreApp::MemoryTelemetry::log(
                                    "Post-Reopen Baseline:    " +
                                    FluidCoreApp::MemoryTelemetry::formatMB(
                                        s3->postReopenBaselinePriv));

                                long long reopenDelta =
                                    static_cast<long long>(s3->postReopenBaselinePriv) -
                                    static_cast<long long>(s3->postClose1Priv);
                                FluidCoreApp::MemoryTelemetry::log(
                                    std::string("Reopen Delta (vs Post-Close): ") +
                                    (reopenDelta >= 0 ? "+" : "-") +
                                    FluidCoreApp::MemoryTelemetry::formatMB(std::abs(reopenDelta)));

                                // Part F: Run Full Document Search 2 on reopened document
                                FluidCoreApp::MemoryTelemetry::log(
                                    "\n[Audit Step 6] Launching Full Document Search 2 on Reopened "
                                    "Document...");
                                const std::size_t beforeSearch2 =
                                    FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                                s3->ctx->pane->performSearch(
                                    "futures", false, false, [s3, beforeSearch2]() {
                                        s3->search2EndPriv =
                                            FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "[Audit Step 6] Search 2 Completed. Private: " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(beforeSearch2) +
                                            " -> " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                s3->search2EndPriv) +
                                            " (Hits: " +
                                            std::to_string(s3->ctx->pane->searchHits().size()) +
                                            ")");

                                        s3->ctx->pane->closeSearch();

                                        // Part G: Final Teardown and Summary Report
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "\n==================== [REOPEN & FIND_TEXT AUDIT: "
                                            "FINAL REPORT] ====================");
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "| Metric | Run 1 (Fresh Open) | Run 2 (Reopened) | "
                                            "Delta (Run 2 vs Run 1) |");
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "|:---|:---:|:---:|:---:|");
                                        long long baseDelta =
                                            static_cast<long long>(s3->postReopenBaselinePriv) -
                                            static_cast<long long>(s3->initialBaselinePriv);
                                        long long searchGrowthDelta =
                                            static_cast<long long>(s3->search2EndPriv) -
                                            static_cast<long long>(s3->search1EndPriv);
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "| Baseline (Post-Load) | " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                s3->initialBaselinePriv) +
                                            " | " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                s3->postReopenBaselinePriv) +
                                            " | " + (baseDelta >= 0 ? "+" : "-") +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                std::abs(baseDelta)) +
                                            " |");
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "| Post-Search Private  | " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                s3->search1EndPriv) +
                                            " | " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                s3->search2EndPriv) +
                                            " | " + (searchGrowthDelta >= 0 ? "+" : "-") +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                std::abs(searchGrowthDelta)) +
                                            " |");

                                        s3->ctx->pane->closeDocument();
                                        const std::size_t finalPriv =
                                            FluidCoreApp::MemoryTelemetry::getProcessPrivateBytes();
                                        FluidCoreApp::MemoryTelemetry::log(
                                            "| Post-Close Final     | " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(
                                                s3->postClose1Priv) +
                                            " | " +
                                            FluidCoreApp::MemoryTelemetry::formatMB(finalPriv) +
                                            " | - |");
                                        FluidCoreApp::MemoryTelemetry::runHeapMin(
                                            "Final Teardown HeapMin");

                                        GtkApplication* app = s3->ctx->app;
                                        delete s3;
                                        if (app) {
                                            g_application_quit(G_APPLICATION(app));
                                        }
                                    });
                                return G_SOURCE_REMOVE;
                            },
                            s2);
                        return G_SOURCE_REMOVE;
                    },
                    s);
            });
            return G_SOURCE_REMOVE;
        },
        state);
}

void onActivate(GtkApplication* app, gpointer userData) {
    auto* context = static_cast<AppContext*>(userData);

    std::string activePdfPath = (context->pdfPath ? *context->pdfPath : "");
    size_t activePage = 0;
    if (activePdfPath.empty() && context->projectPath && !context->projectPath->empty() &&
        context->engine && context->engine->isProjectOpen()) {
        auto docs = context->engine->projectStore().listDocuments();
        if (!docs.empty()) {
            std::filesystem::path bundle(*context->projectPath);
            activePdfPath = (bundle / docs[0].relativePath).string();
            if (docs[0].pageCount > 0 && docs[0].lastViewedPage < docs[0].pageCount) {
                activePage = docs[0].lastViewedPage;
            } else if (docs[0].pageCount > 0) {
                activePage = docs[0].pageCount - 1;
            }
        }
    }

    // Widgets may only be created after gtk_init(), which happens inside
    // g_application_run() — so views are built here, not in main().
    auto* documentPane = new FluidCoreApp::DocumentPane(activePdfPath, activePage);
    g_object_set_data_full(
        G_OBJECT(app), "document-pane", documentPane,
        +[](gpointer data) { delete static_cast<FluidCoreApp::DocumentPane*>(data); });

    auto* workspace = new FluidCoreApp::WorkspaceView(*context->engine);
    g_object_set_data_full(
        G_OBJECT(app), "workspace-view", workspace,
        +[](gpointer data) { delete static_cast<FluidCoreApp::WorkspaceView*>(data); });

    auto* palmEngine = new FluidCore::PalmRejectionEngine();
    g_object_set_data_full(
        G_OBJECT(app), "palm-rejection-engine", palmEngine,
        +[](gpointer data) { delete static_cast<FluidCore::PalmRejectionEngine*>(data); });

    documentPane->setPalmRejectionEngine(palmEngine);
    workspace->setPalmRejectionEngine(palmEngine);

    // Tool synchronization service & Top modern toolbar
    auto* toolManager = new FluidCoreApp::ToolManager();
    g_object_set_data_full(
        G_OBJECT(app), "tool-manager", toolManager,
        +[](gpointer data) { delete static_cast<FluidCoreApp::ToolManager*>(data); });

    auto* topToolbar = new FluidCoreApp::TopToolbarWidget(*toolManager);
    g_object_set_data_full(
        G_OBJECT(app), "top-toolbar", topToolbar,
        +[](gpointer data) { delete static_cast<FluidCoreApp::TopToolbarWidget*>(data); });

    toolManager->addChangeListener([documentPane, workspace, toolManager](FluidCoreApp::Tool tool) {
        const char* toolStr = FluidCoreApp::ToolManager::toolToString(tool);
        if (documentPane) {
            documentPane->setTool(toolStr);
        }
        if (workspace) {
            workspace->setTool(toolStr);
        }
        if (FluidCoreApp::ToolManager::isInkingTool(tool)) {
            const auto props = toolManager->propertiesForTool(tool);
            if (documentPane) {
                documentPane->setColor(props.color);
                documentPane->setStrokeWidth(props.width);
            }
            if (workspace) {
                workspace->setColor(props.color);
                workspace->setStrokeWidth(props.width);
            }
        }
    });

    toolManager->addPropertyChangeListener(
        [documentPane, workspace](FluidCoreApp::Tool, const FluidCoreApp::InkProperties& props) {
            if (documentPane) {
                documentPane->setColor(props.color);
                documentPane->setStrokeWidth(props.width);
            }
            if (workspace) {
                workspace->setColor(props.color);
                workspace->setStrokeWidth(props.width);
            }
        });

    const auto initialProps = toolManager->propertiesForTool(toolManager->activeTool());
    const char* initToolStr = FluidCoreApp::ToolManager::toolToString(toolManager->activeTool());
    if (documentPane) {
        documentPane->setTool(initToolStr);
        documentPane->setColor(initialProps.color);
        documentPane->setStrokeWidth(initialProps.width);
    }
    if (workspace) {
        workspace->setTool(initToolStr);
        workspace->setColor(initialProps.color);
        workspace->setStrokeWidth(initialProps.width);
    }

    // Multi-document resolution and high-DPI crop tile cache
    auto* pdfDocService = new FluidCoreApp::PdfDocumentService();
    g_object_set_data_full(
        G_OBJECT(app), "pdf-doc-service", pdfDocService,
        +[](gpointer data) { delete static_cast<FluidCoreApp::PdfDocumentService*>(data); });

    if (context->projectPath && !context->projectPath->empty() && context->engine &&
        context->engine->isProjectOpen()) {
        auto docs = context->engine->projectStore().listDocuments();
        std::filesystem::path bundle(*context->projectPath);
        for (const auto& doc : docs) {
            std::string dPath = (bundle / doc.relativePath).string();
            if (documentPane->document() && doc.docId == docs[0].docId) {
                pdfDocService->registerMainDocument(doc.docId, documentPane->document(), dPath);
            } else {
                pdfDocService->registerMainDocument(doc.docId, nullptr, dPath);
            }
        }
    } else if (documentPane->document()) {
        pdfDocService->registerMainDocument(documentPane->docId(), documentPane->document(),
                                            activePdfPath);
        pdfDocService->registerMainDocument("doc-primary.pdf", documentPane->document(),
                                            activePdfPath);
        if (!activePdfPath.empty()) {
            pdfDocService->registerMainDocument(activePdfPath, documentPane->document(),
                                                activePdfPath);
        }
    }

    auto* excerptTileCache = new FluidCoreApp::ExcerptTileCache(*pdfDocService);
    g_object_set_data_full(
        G_OBJECT(app), "excerpt-tile-cache", excerptTileCache,
        +[](gpointer data) { delete static_cast<FluidCoreApp::ExcerptTileCache*>(data); });

    workspace->setExcerptTileCache(excerptTileCache);

    excerptTileCache->setStrokeProvider(
        [documentPane, engine = context->engine](const std::string& docId, std::size_t pageNo,
                                                 const FluidCore::Rectangle& cropNormRect,
                                                 std::vector<FluidCore::Stroke>& outStrokes) {
            if (!documentPane) {
                return;
            }

            std::vector<std::string> allDocPaths;
            if (engine && engine->isProjectOpen()) {
                for (const auto& d : engine->projectStore().listDocuments()) {
                    allDocPaths.push_back(d.docId);
                    allDocPaths.push_back(d.relativePath);
                }
            }

            if (!documentPane->matchesDocId(docId, allDocPaths)) {
                return;
            }

            double pw = 0.0, ph = 0.0;
            if (!documentPane->getPageDimensions(pageNo, &pw, &ph)) {
                std::cerr << "[CropFilter] ERROR: missing page geometry for doc=" << docId
                          << " p=" << pageNo << " (aborting filtering)\n";
                return;
            }

            FluidCore::Rectangle cropPdfRect{cropNormRect.x * pw, cropNormRect.y * ph,
                                             cropNormRect.w * pw, cropNormRect.h * ph};

            const auto& allStrokes = documentPane->annotationStore().strokes();
            for (const auto& s : allStrokes) {
                if (s.pageIndex == pageNo) {
                    if (FluidCore::rectanglesIntersect(FluidCore::computeStrokeBounds(s),
                                                       cropPdfRect)) {
                        outStrokes.push_back(s);
                    }
                }
            }
        });

    documentPane->setOnAnnotationsChangedSpatialCallback(
        [excerptTileCache, workspace](const std::string& docId, std::size_t pageNo,
                                      const FluidCore::Rectangle& changedNormRect) {
            if (excerptTileCache) {
                excerptTileCache->invalidateSpatial(docId, pageNo, changedNormRect);
            }
            if (workspace && workspace->drawingArea() && GTK_IS_WIDGET(workspace->drawingArea())) {
                gtk_widget_queue_draw(workspace->drawingArea());
            }
        });

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

    // Set application window icon if available
    {
        std::error_code ec;
        std::vector<std::string> iconCandidates = {"fluidcore.png", "share/icons/fluidcore.png",
                                                   "resources/icons/fluidcore.png"};
        for (const auto& candidate : iconCandidates) {
            if (std::filesystem::exists(candidate, ec)) {
                gtk_window_set_icon_from_file(GTK_WINDOW(window), candidate.c_str(), nullptr);
                break;
            }
        }
    }

    auto* headerBar = new FluidCoreApp::AppHeaderBar(GTK_WINDOW(window));
    g_object_set_data_full(
        G_OBJECT(app), "app-header-bar", headerBar,
        +[](gpointer data) { delete static_cast<FluidCoreApp::AppHeaderBar*>(data); });

    // Track active pane recency for intelligent undo/redo routing
    auto* lastActivePane = new ActivePane(ActivePane::Workspace);
    g_object_set_data_full(
        G_OBJECT(app), "last-active-pane", lastActivePane,
        +[](gpointer data) { delete static_cast<ActivePane*>(data); });

    auto* viewCtx = new AppViewContext{documentPane,
                                       workspace,
                                       toolManager,
                                       context->engine,
                                       pdfDocService,
                                       excerptTileCache,
                                       headerBar,
                                       GTK_WINDOW(window),
                                       lastActivePane,
                                       nullptr,
                                       false,
                                       nullptr,
                                       context->runScenarioA,
                                       context->runScenarioB,
                                       context->runScenarioRepeatedFind,
                                       context->runScenarioReopenAudit,
                                       app,
                                       context->repeatedFindIterations};

    auto updateUndoRedoUI = [topToolbar, workspace, documentPane, lastActivePane, headerBar,
                             viewCtx]() {
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
            bool hasUndoableChanges =
                (workspace && workspace->canUndo()) || (documentPane && documentPane->canUndo());
            bool dirty = viewCtx->isProjectDirty || hasUndoableChanges;
            headerBar->setSaveStatus(dirty ? FluidCoreApp::SaveStatus::Unsaved
                                           : FluidCoreApp::SaveStatus::Saved);
        }
    };
    viewCtx->updateUndoRedoUI = updateUndoRedoUI;

    workspace->undoStack().setChangeListener(
        [lastActivePane, updateUndoRedoUI, viewCtx, workspace]() {
            *lastActivePane = ActivePane::Workspace;
            if (workspace && workspace->canUndo()) {
                viewCtx->isProjectDirty = true;
            } else if (workspace && !workspace->canUndo()) {
                viewCtx->isProjectDirty = false;
            }
            updateUndoRedoUI();
        });
    documentPane->undoStack().setChangeListener(
        [lastActivePane, updateUndoRedoUI, viewCtx, documentPane]() {
            *lastActivePane = ActivePane::Document;
            if (documentPane && documentPane->canUndo()) {
                viewCtx->isProjectDirty = true;
            } else if (documentPane && !documentPane->canUndo()) {
                viewCtx->isProjectDirty = false;
            }
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

    g_object_set_data_full(
        G_OBJECT(app), "app-view-context", viewCtx,
        +[](gpointer data) { delete static_cast<AppViewContext*>(data); });

    // Wire HeaderBar callbacks
    headerBar->setOnNewProject([viewCtx]() { performNewProject(viewCtx); });
    headerBar->setOnOpenPdf([viewCtx]() { performOpenPdf(viewCtx); });
    headerBar->setOnOpenProject([viewCtx]() { performOpenProject(viewCtx); });
    headerBar->setOnSaveProject([viewCtx]() { performSaveProject(viewCtx); });
    headerBar->setOnSaveProjectAs([viewCtx]() { performSaveProject(viewCtx); });
    headerBar->setOnExport([viewCtx]() { performExport(viewCtx); });

    // Initialize HeaderBar Project Title
    if (context->projectPath && !context->projectPath->empty()) {
        headerBar->setProjectTitle(context->engine ? context->engine->projectTitle()
                                                   : "Untitled Project",
                                   *context->projectPath);
    } else {
        std::string sub =
            context->pdfPath && !context->pdfPath->empty() ? *context->pdfPath : "Workspace Canvas";
        headerBar->setProjectTitle(
            context->engine ? context->engine->projectTitle() : "Untitled Project", sub);
    }
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

    topToolbar->setOnToggleMinimap([workspace]() {
        const bool newVisible = !workspace->isMinimapVisible();
        workspace->setMinimapVisible(newVisible);
    });

    workspace->setOnMinimapVisibilityChanged(
        [topToolbar](bool visible) { topToolbar->setMinimapActive(visible); });

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

    // Wire Ctrl+Shift+S (Save As) action to unified save
    GSimpleAction* saveAsAction = g_simple_action_new("save_as", nullptr);
    g_signal_connect(saveAsAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         performSaveProject(static_cast<AppViewContext*>(data));
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
                             ctx->toolManager->toggleEraser();
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
                performSaveProject(ctx);
                return TRUE;
            }
            if (ctrl && !shift && (event->keyval == GDK_KEY_e || event->keyval == GDK_KEY_E)) {
                performExport(ctx);
                return TRUE;
            }
            if (ctrl && !shift && !alt &&
                (event->keyval == GDK_KEY_m || event->keyval == GDK_KEY_M)) {
                if (ws) {
                    const bool newVisible = !ws->isMinimapVisible();
                    ws->setMinimapVisible(newVisible);
                }
                return TRUE;
            }

            // Undo / Redo fallback accelerators
            if (ctrl && !shift && !alt &&
                (event->keyval == GDK_KEY_z || event->keyval == GDK_KEY_Z)) {
                g_action_group_activate_action(G_ACTION_GROUP(windowWidget), "undo", nullptr);
                return TRUE;
            }
            if ((ctrl && shift && !alt &&
                 (event->keyval == GDK_KEY_z || event->keyval == GDK_KEY_Z)) ||
                (ctrl && !shift && !alt &&
                 (event->keyval == GDK_KEY_y || event->keyval == GDK_KEY_Y))) {
                g_action_group_activate_action(G_ACTION_GROUP(windowWidget), "redo", nullptr);
                return TRUE;
            }

            // Quick Esc handling: cancel current drag/search and reset tool to Select
            if (event->keyval == GDK_KEY_Escape) {
                if (ws) {
                    ws->cancelCurrentInteraction();
                }
                if (pane) {
                    pane->clearTextSelection();
                    pane->clearCropSelection();
                }
                if (tm) {
                    tm->setActiveTool(FluidCoreApp::Tool::Select);
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

            // Global Zoom shortcuts: Ctrl++, Ctrl+=, Ctrl+KP_Add, Ctrl+-, Ctrl+_, Ctrl+KP_Subtract,
            // Ctrl+0, Ctrl+KP_0
            if (ctrl && !shift && !alt &&
                (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal ||
                 event->keyval == GDK_KEY_KP_Add)) {
                if (ctx->lastActivePane && *ctx->lastActivePane == ActivePane::Workspace) {
                    if (ws) {
                        GtkAllocation alloc;
                        gtk_widget_get_allocation(ws->widget(), &alloc);
                        const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
                        const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
                        ws->zoomAt(1.2, cx, cy);
                    }
                } else if (pane) {
                    pane->zoomIn();
                }
                return TRUE;
            }

            if (ctrl && !shift && !alt &&
                (event->keyval == GDK_KEY_minus || event->keyval == GDK_KEY_underscore ||
                 event->keyval == GDK_KEY_KP_Subtract)) {
                if (ctx->lastActivePane && *ctx->lastActivePane == ActivePane::Workspace) {
                    if (ws) {
                        GtkAllocation alloc;
                        gtk_widget_get_allocation(ws->widget(), &alloc);
                        const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
                        const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
                        ws->zoomAt(0.8333, cx, cy);
                    }
                } else if (pane) {
                    pane->zoomOut();
                }
                return TRUE;
            }

            if (ctrl && !shift && !alt &&
                (event->keyval == GDK_KEY_0 || event->keyval == GDK_KEY_KP_0)) {
                if (ctx->lastActivePane && *ctx->lastActivePane == ActivePane::Workspace) {
                    if (ws) {
                        ws->resetView();
                    }
                } else if (pane) {
                    pane->resetZoom();
                }
                return TRUE;
            }

            // Alt+1 to Alt+6 tool accelerators
            if (!ctrl && alt && !shift) {
                if (event->keyval == GDK_KEY_1) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Pen);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_2) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Highlighter);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_3) {
                    if (tm)
                        tm->toggleEraser();
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_4) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Select);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_5) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Crop);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_6) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Connector);
                    return TRUE;
                }
            }

            // Quick single-key tool switching, keypad zoom, and Delete/Backspace
            if (!ctrl && !alt && !shift) {
                GtkWidget* focusWidget = gtk_window_get_focus(GTK_WINDOW(windowWidget));
                if (focusWidget && GTK_IS_ENTRY(focusWidget)) {
                    return FALSE;
                }

                if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete ||
                    event->keyval == GDK_KEY_BackSpace) {
                    if (ws && (ws->hasSelectedNode() || ws->hasSelectedEdge())) {
                        ws->deleteSelected();
                        return TRUE;
                    }
                }

                if (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal ||
                    event->keyval == GDK_KEY_KP_Add) {
                    if (ctx->lastActivePane && *ctx->lastActivePane == ActivePane::Workspace) {
                        if (ws) {
                            GtkAllocation alloc;
                            gtk_widget_get_allocation(ws->widget(), &alloc);
                            const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
                            const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
                            ws->zoomAt(1.2, cx, cy);
                        }
                    } else if (pane) {
                        pane->zoomIn();
                    }
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_minus || event->keyval == GDK_KEY_KP_Subtract) {
                    if (ctx->lastActivePane && *ctx->lastActivePane == ActivePane::Workspace) {
                        if (ws) {
                            GtkAllocation alloc;
                            gtk_widget_get_allocation(ws->widget(), &alloc);
                            const double cx = alloc.width > 0 ? alloc.width / 2.0 : 400.0;
                            const double cy = alloc.height > 0 ? alloc.height / 2.0 : 300.0;
                            ws->zoomAt(0.8333, cx, cy);
                        }
                    } else if (pane) {
                        pane->zoomOut();
                    }
                    return TRUE;
                }

                if (event->keyval == GDK_KEY_space) {
                    if (ws)
                        ws->setSpacePressed(true);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S ||
                    event->keyval == GDK_KEY_F4) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Select);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_p || event->keyval == GDK_KEY_P ||
                    event->keyval == GDK_KEY_b || event->keyval == GDK_KEY_B ||
                    event->keyval == GDK_KEY_F1) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Pen);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H ||
                    event->keyval == GDK_KEY_F2) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Highlighter);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_e || event->keyval == GDK_KEY_E ||
                    event->keyval == GDK_KEY_F3) {
                    if (tm)
                        tm->toggleEraser();
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C ||
                    event->keyval == GDK_KEY_F5) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Crop);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_l || event->keyval == GDK_KEY_L ||
                    event->keyval == GDK_KEY_a || event->keyval == GDK_KEY_A ||
                    event->keyval == GDK_KEY_F6) {
                    if (tm)
                        tm->setActiveTool(FluidCoreApp::Tool::Connector);
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_bracketleft) {
                    if (tm) {
                        double delta = (tm->activeTool() == FluidCoreApp::Tool::Highlighter)
                                           ? -FluidCoreApp::InkingLimits::HighlighterStepWidth
                                           : -FluidCoreApp::InkingLimits::PenStepWidth;
                        tm->stepActiveWidth(delta);
                    }
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_bracketright) {
                    if (tm) {
                        double delta = (tm->activeTool() == FluidCoreApp::Tool::Highlighter)
                                           ? FluidCoreApp::InkingLimits::HighlighterStepWidth
                                           : FluidCoreApp::InkingLimits::PenStepWidth;
                        tm->stepActiveWidth(delta);
                    }
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
    gtk_box_pack_start(GTK_BOX(rootBox), headerBar->widget(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), topToolbar->widget(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), paned, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), rootBox);
    gtk_window_set_title(GTK_WINDOW(window), "FluidCore");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_widget_show_all(window);
    gtk_window_deiconify(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(window));

    updateUndoRedoUI();

    std::cout << "[FluidCore] Window ready and presented (" << documentPane->pages().size()
              << " pages loaded)." << std::endl;

    if (g_getenv("FLUIDCORE_HEARTBEAT") != nullptr) {
        g_timeout_add_seconds(
            4,
            +[](gpointer data) -> gboolean {
                auto* vCtx = static_cast<AppViewContext*>(data);
                if (!vCtx)
                    return G_SOURCE_CONTINUE;

                const auto m = FluidCoreApp::MemoryTelemetry::getHeapMetrics();

                std::string msg =
                    "[Heartbeat] Priv: " + FluidCoreApp::MemoryTelemetry::formatMB(m.privateBytes) +
                    " | WS: " + FluidCoreApp::MemoryTelemetry::formatMB(m.workingSet) +
                    " | LiveAlloc: " + FluidCoreApp::MemoryTelemetry::formatMB(m.heapAllocated) +
                    " | HeapCommit: " + FluidCoreApp::MemoryTelemetry::formatMB(m.heapCommitted) +
                    " | Slack: " + FluidCoreApp::MemoryTelemetry::formatMB(m.heapSlack) +
                    " | Poppler Live: " +
                    std::to_string(FluidCoreApp::PopplerLifetimeTracker::getLivePages());

                if (vCtx->pane) {
                    auto ptStats = vCtx->pane->pageTileCache().getStats();
                    msg += " | PageTileCache: " +
                           FluidCoreApp::MemoryTelemetry::formatMB(ptStats.currentBytes) + " (" +
                           std::to_string(ptStats.entryCount) + " pgs)";
                }
                if (vCtx->excerptTileCache) {
                    auto etcStats = vCtx->excerptTileCache->getStats();
                    msg += " | ExcerptTileCache: " +
                           FluidCoreApp::MemoryTelemetry::formatMB(etcStats.currentBytes) + " (" +
                           std::to_string(etcStats.entryCount) + " crops, " +
                           std::to_string(etcStats.activeRequests) + " reqs)";
                }
                FluidCoreApp::MemoryTelemetry::log(msg);
                return G_SOURCE_CONTINUE;
            },
            viewCtx);
    }

    if (viewCtx->runScenarioReopenAudit) {
        scheduleScenarioReopenAudit(viewCtx);
    } else if (viewCtx->runScenarioRepeatedFind) {
        scheduleScenarioRepeatedFind(viewCtx, viewCtx->repeatedFindIterations);
    } else if (viewCtx->runScenarioA) {
        scheduleScenarioA(viewCtx);
    } else if (viewCtx->runScenarioB) {
        scheduleScenarioB(viewCtx);
    }
}

} // namespace

int main(int argc, char** argv) {
#ifdef FLUIDCORE_HAS_MIMALLOC
    mi_option_set(mi_option_purge_delay, 0);
    mi_option_set(mi_option_purge_decommits, 1);
#endif

#ifdef _WIN32
    // If launched from an existing terminal, attach to it so stdout/stderr work.
    // When launched from Explorer, Desktop, or Start Menu, AttachConsole fails silently
    // and no console window is created.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);
    }

    HRESULT hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif

#ifndef _WIN32
    installCrashHandlers();
#else
    // On Windows, set default paths for bundled resources if not already in environment
    if (argc > 0 && argv[0]) {
        std::error_code ec;
        std::filesystem::path exePath = std::filesystem::absolute(argv[0], ec);
        std::filesystem::path appDir = exePath.parent_path();

        if (!g_getenv("GSETTINGS_SCHEMA_DIR")) {
            auto schemaDir = appDir / "share" / "glib-2.0" / "schemas";
            if (std::filesystem::exists(schemaDir, ec)) {
                g_setenv("GSETTINGS_SCHEMA_DIR", schemaDir.string().c_str(), TRUE);
            }
        }
        if (!g_getenv("XDG_DATA_DIRS")) {
            auto shareDir = appDir / "share";
            if (std::filesystem::exists(shareDir, ec)) {
                g_setenv("XDG_DATA_DIRS", shareDir.string().c_str(), TRUE);
            }
        }
        if (!g_getenv("FONTCONFIG_PATH") && !g_getenv("FONTCONFIG_FILE")) {
            auto fontsDir = appDir / "etc" / "fonts";
            if (std::filesystem::exists(fontsDir / "fonts.conf", ec)) {
                g_setenv("FONTCONFIG_PATH", fontsDir.string().c_str(), TRUE);
            } else if (std::filesystem::exists("C:/msys64/ucrt64/etc/fonts/fonts.conf", ec)) {
                g_setenv("FONTCONFIG_PATH", "C:/msys64/ucrt64/etc/fonts", TRUE);
            }
        }
        if (!g_getenv("GDK_PIXBUF_MODULE_FILE")) {
            auto loadersFile = appDir / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders.cache";
            if (std::filesystem::exists(loadersFile, ec)) {
                g_setenv("GDK_PIXBUF_MODULE_FILE", loadersFile.string().c_str(), TRUE);
            }
        }
    }
#endif
    // Suppress known spurious GLib-GIO critical warnings when enumerating WSL DrvFS mounts (/mnt/c,
    // /mnt/d), Win32 volume roots, and win32 dbus warnings
    auto filterLogFunc = [](const gchar* log_domain, GLogLevelFlags log_level, const gchar* message,
                            gpointer user_data) {
        if (message && (std::strstr(message, "standard::size") ||
                        std::strstr(message, "g_file_info_get_size") ||
                        std::strstr(message, "standard::type") ||
                        std::strstr(message, "g_file_info_get_file_type") ||
                        std::strstr(message, "win32 session dbus binary not found"))) {
            return; // Silence harmless GIO volume attribute queries and win32 dbus warning
        }
        g_log_default_handler(log_domain, log_level, message, user_data);
    };

    g_log_set_default_handler(filterLogFunc, nullptr);
    g_log_set_handler(
        "GLib-GIO",
        static_cast<GLogLevelFlags>(G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        filterLogFunc, nullptr);
    g_log_set_handler(
        "GLib",
        static_cast<GLogLevelFlags>(G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        filterLogFunc, nullptr);
    g_log_set_handler(
        nullptr,
        static_cast<GLogLevelFlags>(G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        filterLogFunc, nullptr);

    bool runScenarioA = false;
    bool runScenarioB = false;
    bool runScenarioRepeatedFind = false;
    bool runScenarioReopenAudit = false;
    int repeatedFindIterations = 20;
    std::string rawArg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--run-scenario-a") {
            runScenarioA = true;
        } else if (arg == "--run-scenario-b") {
            runScenarioB = true;
        } else if (arg == "--run-scenario-repeated-find" || arg == "--run-scenario-find20") {
            runScenarioRepeatedFind = true;
            repeatedFindIterations = 20;
        } else if (arg == "--run-scenario-find5") {
            runScenarioRepeatedFind = true;
            repeatedFindIterations = 5;
        } else if (arg.rfind("--find-iters=", 0) == 0) {
            repeatedFindIterations = std::stoi(arg.substr(13));
        } else if (arg == "--run-scenario-reopen" || arg == "--run-scenario-reopen-audit") {
            runScenarioReopenAudit = true;
        } else if (arg == "--null-sink") {
            FluidCoreApp::PageTileCache::setNullSinkMode(true);
        } else if (rawArg.empty() && arg.rfind("--", 0) != 0) {
            rawArg = arg;
        }
    }

    if (const char* envIt = g_getenv("FLUIDCORE_FIND_ITERATIONS")) {
        int v = std::atoi(envIt);
        if (v > 0)
            repeatedFindIterations = v;
    }

    if (const char* envNull = g_getenv("FLUIDCORE_NULL_SINK")) {
        if (std::string(envNull) == "1" || std::string(envNull) == "true") {
            FluidCoreApp::PageTileCache::setNullSinkMode(true);
        }
    }

    // Log complete runtime configuration banner
    const char* envTel = g_getenv("FLUIDCORE_LOG_TELEMETRY");
    const char* envHb = g_getenv("FLUIDCORE_HEARTBEAT");
    const char* envEphem = g_getenv("FLUIDCORE_EPHEMERAL_SEARCH");
    const char* envBench = g_getenv("FLUIDCORE_SEARCH_BENCHMARK");
    const char* envVerb = g_getenv("FLUIDCORE_VERBOSE_TELEMETRY");

    std::string banner = "[Startup] === FLUIDCORE RUNTIME CONFIGURATION ===\n";
    banner += "    FLUIDCORE_LOG_TELEMETRY:      " + std::string(envTel ? envTel : "0") +
              (envTel && std::string(envTel) != "0"
                   ? " (ACTIVE - writing to " +
                         FluidCoreApp::MemoryTelemetry::getTelemetryLogPath() + ")\n"
                   : " (Disabled)\n");
    banner += "    FLUIDCORE_HEARTBEAT:          " + std::string(envHb ? envHb : "0") +
              (envHb && std::string(envHb) != "0" ? " (ACTIVE - 4s interval)\n" : " (Disabled)\n");
    banner += "    FLUIDCORE_EPHEMERAL_SEARCH:   " + std::string(envEphem ? envEphem : "0") +
              (envEphem && std::string(envEphem) != "0"
                   ? " (ACTIVE - throwaway search document per query)\n"
                   : " (Disabled - persistent m_document search)\n");
    banner += "    FLUIDCORE_SEARCH_BENCHMARK:   " + std::string(envBench ? envBench : "0") +
              (envBench && std::string(envBench) != "0" ? " (ACTIVE - stage memory queries)\n"
                                                        : " (Disabled)\n");
    banner +=
        "    FLUIDCORE_VERBOSE_TELEMETRY:  " + std::string(envVerb ? envVerb : "0") +
        (envVerb && std::string(envVerb) != "0" ? " (ACTIVE - batch logs)\n" : " (Disabled)\n");
    banner += "    FLUIDCORE_NULL_SINK:          " +
              std::string(FluidCoreApp::PageTileCache::isNullSinkMode()
                              ? "1 (ACTIVE - no Cairo tiles cached)\n"
                              : "0 (Disabled - Cairo surfaces cached in LRU)\n");
#ifdef FLUIDCORE_HAS_MIMALLOC
    banner += "    MIMALLOC_ALLOCATOR:           1 (ACTIVE - PurgeDelay=0, Decommit=1, Heap Slack "
              "Optimized)";
#else
    banner += "    MIMALLOC_ALLOCATOR:           0 (Disabled - Standard CRT Heap)";
#endif
    FluidCoreApp::MemoryTelemetry::log(banner);

    const std::string inputPath = normalizePath(rawArg);

    std::string pdfPath;
    std::string projectPath;

    std::error_code ec;
    bool isLtproj = false;
    if (!inputPath.empty()) {
        if (inputPath.size() >= 7 && inputPath.substr(inputPath.size() - 7) == ".ltproj") {
            isLtproj = true;
        } else if (std::filesystem::is_directory(inputPath, ec) &&
                   std::filesystem::exists(std::filesystem::path(inputPath) / "project.db", ec)) {
            isLtproj = true;
        }
    }

    FluidCoreEngine engine("default-project");

    if (isLtproj) {
        projectPath = inputPath;
        std::string openErr;
        if (!engine.openProjectWithError(projectPath, &openErr)) {
            std::cerr << "[FluidCore] Failed to open project " << projectPath << ": " << openErr
                      << std::endl;
        } else {
            std::cout << "[FluidCore] Loaded project: " << projectPath << " ("
                      << engine.projectTitle() << ")" << std::endl;
        }
    } else {
        pdfPath = inputPath;
        seedDemoContent(engine, pdfPath);
    }

    AppContext context{&engine,
                       &pdfPath,
                       &projectPath,
                       runScenarioA,
                       runScenarioB,
                       runScenarioRepeatedFind,
                       runScenarioReopenAudit,
                       repeatedFindIterations};

    std::cout << "[FluidCore] Starting application with "
              << (!projectPath.empty() ? ("project: " + projectPath)
                                       : ("document: " + (pdfPath.empty() ? "(none)" : pdfPath)))
              << std::endl;

    GtkApplication* app = gtk_application_new("org.fluidcore.platform", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &context);
    const int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);

#ifdef _WIN32
    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
#endif

    return status;
}
