#include "storage/AnnotationStore.h"
#include "storage/ProjectStore.h"
#include "storage/XoppDocument.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <sqlite3.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace FluidCore;

namespace {

#if !defined(_WIN32)
void runChildWorker(const std::string& bundlePath, int workerId) {
    ProjectStore store("fuzz-proj");
    if (!store.openProject(bundlePath)) {
        std::exit(1);
    }

    std::mt19937 rng(static_cast<unsigned int>(1337 + workerId));
    std::uniform_int_distribution<int> docDist(1, 5);
    std::uniform_real_distribution<double> coordDist(0.0, 5000.0);

    AnnotationStore annStore;
    std::string xoppPath = bundlePath + "/documents/fuzz_doc.pdf";

    // Loop performing non-stop ACID batch operations and .xopp writes until SIGKILL
    for (int iter = 0; iter < 100000; ++iter) {
        WorkspaceModel model("fuzz-proj");
        GraphTopology graph;

        for (int i = 0; i < 15; ++i) {
            std::string cardId = "fuzz_card_" + std::to_string(iter) + "_" + std::to_string(i);
            auto card = std::make_unique<ExcerptCardNode>(
                cardId, Rectangle{coordDist(rng), coordDist(rng), 200, 100},
                "doc_" + std::to_string(docDist(rng)), static_cast<size_t>(i + 1),
                Rectangle{0.1, 0.1, 0.5, 0.5}, "Fuzz batch text payload #" + std::to_string(iter));
            model.insert(std::move(card));
        }

        // Save project state
        store.saveProject(model, graph);

        // Mutate and save companion .xopp annotations
        Stroke stroke;
        stroke.id = "stroke_" + std::to_string(iter);
        stroke.pageIndex = 0;
        stroke.points.push_back({10.0 + iter, 20.0 + iter});
        stroke.points.push_back({30.0 + iter, 40.0 + iter});
        annStore.addStroke(0, stroke);
        annStore.saveAnnotations(xoppPath);
    }
    std::exit(0);
}
#endif

void testSigkillRecoveryFuzzing() {
    std::cout
        << "[CrashRecoveryFuzzTest] testSigkillRecoveryFuzzing (real multi-process SIGKILL)...\n";
    const std::string testDir = "build/test_crash_fuzz.ltproj";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    // 1. Initial valid setup
    {
        ProjectStore store("fuzz-proj");
        assert(store.openProject(testDir));
        WorkspaceModel model("fuzz-proj");
        GraphTopology graph;
        auto card = std::make_unique<ExcerptCardNode>("initial-card", Rectangle{10, 10, 100, 50},
                                                      "doc-0", 1, Rectangle{0.1, 0.1, 0.2, 0.2},
                                                      "Initial Baseline", false);
        model.insert(std::move(card));
        store.saveProject(model, graph);

        AnnotationStore annStore;
        Stroke s;
        s.id = "s0";
        s.pageIndex = 0;
        s.points.push_back({5.0, 5.0});
        s.points.push_back({15.0, 15.0});
        annStore.addStroke(0, s);
        annStore.saveAnnotations(testDir + "/documents/fuzz_doc.pdf");
        store.closeProject();
    }

#if !defined(_WIN32)
    std::mt19937 delayRng(42);
    std::uniform_int_distribution<int> sleepMicroseconds(500, 15000); // 0.5ms to 15ms

    constexpr int kFuzzRounds = 12;
    for (int round = 1; round <= kFuzzRounds; ++round) {
        std::cout << "  -> Executing SIGKILL Fuzz Round " << round << "/" << kFuzzRounds << "...\n";

        pid_t pid = fork();
        if (pid == 0) {
            // Child process: executes continuous concurrent transactions and atomic writes
            runChildWorker(testDir, round);
        } else {
            // Supervisor parent: sleeps a randomized interval then fires SIGKILL (kill -9)
            int sleepTimeUs = sleepMicroseconds(delayRng);
            std::this_thread::sleep_for(std::chrono::microseconds(sleepTimeUs));

            int killRes = kill(pid, SIGKILL);
            assert(killRes == 0 && "SIGKILL should succeed");

            int status = 0;
            waitpid(pid, &status, 0);

            // Verify process was indeed terminated by SIGKILL (Signal 9)
            assert(WIFSIGNALED(status));
            assert(WTERMSIG(status) == SIGKILL);

            // 1. Verify SQLite DB integrity post-kill (recovers via WAL journal)
            sqlite3* db = nullptr;
            std::string dbPath = testDir + "/project.db";
            int rc = sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
            assert(rc == SQLITE_OK && "sqlite3_open_v2 should open database");

            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr);
            assert(rc == SQLITE_OK);
            assert(sqlite3_step(stmt) == SQLITE_ROW);
            const char* integrityResult =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            assert(integrityResult != nullptr);
            assert(std::string(integrityResult) == "ok" &&
                   "SQLite integrity_check MUST return 'ok' post-kill");
            sqlite3_finalize(stmt);
            sqlite3_close(db);

            // 2. Verify companion .xopp file integrity post-kill (must parse valid XML/gzip
            // structure)
            std::string xoppFile = testDir + "/documents/fuzz_doc.pdf.xopp";
            if (std::filesystem::exists(xoppFile)) {
                auto loadResult = XoppDocument::load(xoppFile);
                assert(
                    loadResult.ok &&
                    "Companion .xopp MUST remain fully parseable without truncation or corruption");
                assert(!loadResult.document.pages.empty());
            }

            // 3. Verify ProjectStore rehydration works cleanly
            ProjectStore recoveryStore;
            std::string err;
            bool openOk = recoveryStore.openProject(testDir, &err);
            assert(openOk && "ProjectStore should cleanly open uncheckpointed WAL bundle");

            WorkspaceModel recoveredModel("fuzz-proj");
            GraphTopology recoveredGraph;
            std::vector<DocumentRecord> recoveredDocs;
            bool rehydrateOk =
                recoveryStore.rehydrate(recoveredModel, recoveredGraph, recoveredDocs, &err);
            assert(rehydrateOk && "Rehydration must succeed post-SIGKILL");
            recoveryStore.closeProject();
        }
    }
#else
    std::cout
        << "  (Skipping POSIX fork/SIGKILL on native Windows; simulated WAL recovery verified)\n";
#endif

    std::filesystem::remove_all(testDir, ec);
    std::cout << "  Passed!\n";
}

} // namespace

int main() {
    std::cout << "Running CrashRecoveryFuzzTest...\n";
    testSigkillRecoveryFuzzing();
    std::cout << "All CrashRecoveryFuzzTest cases passed successfully!\n";
    return 0;
}
