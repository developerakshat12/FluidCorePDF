#include "FluidCoreAPI.h"
#include "graph/GraphTopology.h"
#include "storage/AnnotationStore.h"
#include "storage/ProjectStore.h"
#include "undo/UndoStack.h"
#include "undo/WorkspaceCommands.h"
#include "workspace/CanvasStrokeNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

FluidCore::Stroke makeTestStroke(const std::string& id, double x, double y) {
    FluidCore::Stroke stroke;
    stroke.id = id;
    stroke.tool = "pen";
    stroke.color = 0x000000;
    stroke.width = 2.0;
    stroke.points = {{x, y}, {x + 10.0, y + 10.0}};
    stroke.pressures = {1.0, 1.0};
    stroke.timestamp = 1000;
    return stroke;
}

int check(bool condition, const std::string& desc) {
    if (!condition) {
        std::cerr << "FAIL: " << desc << "\n";
        return 1;
    }
    std::cout << "PASS: " << desc << "\n";
    return 0;
}

} // namespace

int main() {
    int failures = 0;
    std::cout << "--- Running StrokePersistenceRegressionTest ---\n";

    // 1. Verify InsertNodeCommand handles colliding IDs by disambiguating rather than dropping
    {
        FluidCore::WorkspaceModel model("proj-1");
        // Pre-existing rehydrated strokes
        model.insert(std::make_unique<FluidCore::CanvasStrokeNode>(makeTestStroke("stroke-1", 10.0, 10.0)));
        model.insert(std::make_unique<FluidCore::CanvasStrokeNode>(makeTestStroke("stroke-2", 30.0, 30.0)));

        failures += check(model.allNodeIds().size() == 2, "initial rehydrated model has 2 strokes");

        // User attempts to draw a new stroke with ID "stroke-1" (due to s_strokeCounter reset)
        auto newStrokeNode = std::make_unique<FluidCore::CanvasStrokeNode>(makeTestStroke("stroke-1", 50.0, 50.0));
        FluidCore::InsertNodeCommand cmd(model, std::move(newStrokeNode));

        bool executed = cmd.execute();
        failures += check(executed, "InsertNodeCommand::execute returns true for conflicting ID");
        failures += check(model.allNodeIds().size() == 3, "new stroke successfully inserted into model without being dropped");
        failures += check(cmd.nodeId() != "stroke-1", "InsertNodeCommand reassigned colliding ID to unique key");

        // Verify undo / redo
        bool undone = cmd.undo();
        failures += check(undone && model.allNodeIds().size() == 2, "undo removes the newly added stroke");
        bool redone = cmd.redo();
        failures += check(redone && model.allNodeIds().size() == 3, "redo restores the newly added stroke");
    }

    // 2. Verify AnnotationStore collision-free ID generation after deletion
    {
        FluidCore::AnnotationStore store;
        store.addStroke(0, makeTestStroke("stroke-1", 10.0, 10.0));
        store.addStroke(0, makeTestStroke("stroke-2", 20.0, 20.0));

        failures += check(store.strokes().size() == 2, "store has 2 strokes");
        store.removeStroke("stroke-1");
        failures += check(store.strokes().size() == 1, "store has 1 stroke after removal");

        // Add stroke with auto-generated ID: must not collide with "stroke-2"
        FluidCore::Stroke emptyIdStroke = makeTestStroke("", 30.0, 30.0);
        std::string newId = store.addStroke(0, std::move(emptyIdStroke));
        failures += check(newId != "stroke-2", "auto-generated stroke ID does not collide with stroke-2");
        failures += check(store.strokes().size() == 2, "store now has 2 strokes with distinct IDs");

        // Add stroke with explicit colliding ID "stroke-2": must disambiguate
        FluidCore::Stroke collidingStroke = makeTestStroke("stroke-2", 40.0, 40.0);
        std::string disambiguatedId = store.addStroke(0, std::move(collidingStroke));
        failures += check(disambiguatedId != "stroke-2", "explicit colliding stroke ID is disambiguated");
        failures += check(store.strokes().size() == 3, "all 3 strokes survive in AnnotationStore");
    }

    // 3. Verify round-trip persistence to SQLite WAL on disk with newly added strokes
    {
        const std::string bundlePath = "stroke_test_bundle.ltproj";
        std::error_code ec;
        std::filesystem::remove_all(bundlePath, ec);

        FluidCore::ProjectStore store("proj-test");
        std::string error;
        bool ok = store.openProject(bundlePath, &error);
        failures += check(ok, "openProject succeeds");

        FluidCore::WorkspaceModel model("proj-test");
        model.insert(std::make_unique<FluidCore::CanvasStrokeNode>(makeTestStroke("stroke-1", 10.0, 10.0)));
        model.insert(std::make_unique<FluidCore::CanvasStrokeNode>(makeTestStroke("stroke-2", 30.0, 30.0)));

        // Insert new stroke via InsertNodeCommand
        FluidCore::InsertNodeCommand cmd(model, std::make_unique<FluidCore::CanvasStrokeNode>(makeTestStroke("stroke-1", 70.0, 70.0)));
        cmd.execute();
        failures += check(model.allNodeIds().size() == 3, "model has 3 strokes before save");

        FluidCore::GraphTopology graph;
        std::vector<FluidCore::DocumentRecord> docs;
        ok = store.saveProject(model, graph, docs, &error);
        failures += check(ok, "saveProject with disambiguated strokes succeeds");

        // Rehydrate in fresh model
        FluidCore::WorkspaceModel rehydratedModel("proj-test");
        FluidCore::GraphTopology rehydratedGraph;
        std::vector<FluidCore::DocumentRecord> rehydratedDocs;
        ok = store.rehydrate(rehydratedModel, rehydratedGraph, rehydratedDocs, &error);
        failures += check(ok, "rehydrate succeeds");
        failures += check(rehydratedModel.allNodeIds().size() == 3, "all 3 strokes rehydrated from disk without data loss");

        store.closeProject();
        std::filesystem::remove_all(bundlePath, ec);
    }

    if (failures == 0) {
        std::cout << "\nALL STROKE PERSISTENCE REGRESSION TESTS PASSED!\n";
    } else {
        std::cerr << "\n" << failures << " TEST(S) FAILED!\n";
    }

    return failures;
}
