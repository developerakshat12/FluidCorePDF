#include "undo/UndoStack.h"
#include "storage/AnnotationStore.h"
#include "undo/AnnotationCommands.h"
#include "undo/Command.h"
#include "undo/WorkspaceCommands.h"
#include "workspace/WorkspaceModel.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << "\n";
        std::abort();
    }
}

class TestValueCommand : public FluidCore::Command {
  public:
    TestValueCommand(int& target, int oldVal, int newVal, std::string desc = "Test Value")
        : m_target(target), m_oldVal(oldVal), m_newVal(newVal), m_desc(std::move(desc)) {}

    bool execute() override {
        m_target = m_newVal;
        return true;
    }

    bool undo() override {
        m_target = m_oldVal;
        return true;
    }

    std::string description() const override { return m_desc; }

  private:
    int& m_target;
    int m_oldVal;
    int m_newVal;
    std::string m_desc;
};

class DummyWorkspaceNode final : public FluidCore::WorkspaceNode {
  public:
    DummyWorkspaceNode(std::string id, FluidCore::Rectangle bounds)
        : m_id(std::move(id)), m_bounds(bounds) {}
    const std::string& id() const override { return m_id; }
    FluidCore::Rectangle bounds() const override { return m_bounds; }

  private:
    std::string m_id;
    FluidCore::Rectangle m_bounds;
};

} // namespace

using FluidCore::AddStrokeCommand;
using FluidCore::AnnotationStore;
using FluidCore::ClearPageStrokesCommand;
using FluidCore::CompoundCommand;
using FluidCore::MoveNodeCommand;
using FluidCore::RemoveStrokeCommand;
using FluidCore::Stroke;
using FluidCore::UndoStack;
using FluidCore::WorkspaceModel;
using FluidCore::XoppPoint;

void testBasicUndoRedo() {
    int value = 0;
    UndoStack stack;

    expect(!stack.canUndo(), "initially cannot undo");
    expect(!stack.canRedo(), "initially cannot redo");

    stack.pushAndExecute(std::make_unique<TestValueCommand>(value, 0, 10, "Set 10"));
    expect(value == 10, "value set to 10");
    expect(stack.canUndo(), "can undo after push");
    expect(!stack.canRedo(), "cannot redo after push");
    expect(stack.undoDescription() == "Set 10", "description match");

    expect(stack.undo(), "undo succeeded");
    expect(value == 0, "value reverted to 0");
    expect(!stack.canUndo(), "no more undo");
    expect(stack.canRedo(), "can redo now");
    expect(stack.redoDescription() == "Set 10", "redo description match");

    expect(stack.redo(), "redo succeeded");
    expect(value == 10, "value reapplied to 10");
    expect(stack.canUndo(), "can undo again");
    expect(!stack.canRedo(), "no more redo");

    std::cout << "[PASS] testBasicUndoRedo\n";
}

void testRedoTruncationOnNewCommand() {
    int value = 0;
    UndoStack stack;

    stack.pushAndExecute(std::make_unique<TestValueCommand>(value, 0, 1, "Cmd 1"));
    stack.pushAndExecute(std::make_unique<TestValueCommand>(value, 1, 2, "Cmd 2"));
    expect(value == 2, "value is 2");

    stack.undo(); // value reverts to 1, redo stack has Cmd 2
    expect(value == 1, "value is 1");
    expect(stack.canRedo(), "can redo Cmd 2");

    // Push Cmd 3 -> should truncate redo stack
    stack.pushAndExecute(std::make_unique<TestValueCommand>(value, 1, 3, "Cmd 3"));
    expect(value == 3, "value is 3");
    expect(!stack.canRedo(), "redo stack must be truncated on new command");

    stack.undo(); // reverts Cmd 3 -> value is 1
    expect(value == 1, "value reverted to 1");
    stack.undo(); // reverts Cmd 1 -> value is 0
    expect(value == 0, "value reverted to 0");

    std::cout << "[PASS] testRedoTruncationOnNewCommand\n";
}

void testMaxDepthCapacityTrimming() {
    int value = 0;
    UndoStack stack(3); // Max depth 3

    for (int i = 1; i <= 5; ++i) {
        stack.pushAndExecute(
            std::make_unique<TestValueCommand>(value, i - 1, i, "Set " + std::to_string(i)));
    }

    expect(stack.undoCount() == 3, "undo count capped at 3");
    expect(value == 5, "current value is 5");

    // Undo should only visit commands 5, 4, 3
    stack.undo(); // 5 -> 4
    expect(value == 4, "value is 4");
    stack.undo(); // 4 -> 3
    expect(value == 3, "value is 3");
    stack.undo(); // 3 -> 2
    expect(value == 2, "value is 2");

    expect(!stack.canUndo(), "commands 1 and 2 were evicted FIFO");

    std::cout << "[PASS] testMaxDepthCapacityTrimming\n";
}

void testCompoundCommand() {
    int v1 = 0;
    int v2 = 0;
    UndoStack stack;

    auto compound = std::make_unique<CompoundCommand>("Batch Update");
    compound->addCommand(std::make_unique<TestValueCommand>(v1, 0, 10, "Set V1"));
    compound->addCommand(std::make_unique<TestValueCommand>(v2, 0, 20, "Set V2"));

    stack.pushAndExecute(std::move(compound));
    expect(v1 == 10, "v1 is 10");
    expect(v2 == 20, "v2 is 20");

    stack.undo();
    expect(v1 == 0, "v1 undone to 0");
    expect(v2 == 0, "v2 undone to 0");

    stack.redo();
    expect(v1 == 10, "v1 redone to 10");
    expect(v2 == 20, "v2 redone to 20");

    std::cout << "[PASS] testCompoundCommand\n";
}

void testAnnotationCommands() {
    AnnotationStore store;
    UndoStack stack;

    Stroke stroke;
    stroke.id = "s-1";
    stroke.color = 0xFF0000;
    stroke.width = 2.0;
    stroke.points = {{10.0, 10.0}, {20.0, 20.0}};

    // 1. AddStrokeCommand
    stack.pushAndExecute(std::make_unique<AddStrokeCommand>(store, 0, stroke));
    expect(store.strokes().size() == 1, "store has 1 stroke");
    expect(store.strokesForPage(0).size() == 1, "page 0 has 1 stroke");

    stack.undo();
    expect(store.strokes().empty(), "undo removes stroke");

    stack.redo();
    expect(store.strokes().size() == 1, "redo restores stroke");
    expect(store.strokes()[0].id == "s-1", "stroke ID preserved across undo/redo");

    // 2. RemoveStrokeCommand (Eraser)
    stack.pushAndExecute(std::make_unique<RemoveStrokeCommand>(store, 0, stroke));
    expect(store.strokes().empty(), "stroke erased");

    stack.undo();
    expect(store.strokes().size() == 1, "erase undone");
    expect(store.strokes()[0].id == "s-1", "restored stroke ID matches");

    stack.redo();
    expect(store.strokes().empty(), "erase redone");

    // 3. ClearPageStrokesCommand
    store.addStroke(0, stroke);
    Stroke stroke2 = stroke;
    stroke2.id = "s-2";
    store.addStroke(0, stroke2);
    expect(store.strokesForPage(0).size() == 2, "2 strokes on page");

    stack.pushAndExecute(std::make_unique<ClearPageStrokesCommand>(store, 0));
    expect(store.strokesForPage(0).empty(), "all strokes cleared");

    stack.undo();
    expect(store.strokesForPage(0).size() == 2, "clear page undone -> 2 strokes restored");

    stack.redo();
    expect(store.strokesForPage(0).empty(), "clear page redone -> strokes cleared");

    std::cout << "[PASS] testAnnotationCommands\n";
}

void testWorkspaceMoveNodeCommand() {
    WorkspaceModel model("proj-1");
    UndoStack stack;

    auto node = std::make_unique<DummyWorkspaceNode>("card-1",
                                                     FluidCore::Rectangle{10.0, 20.0, 100.0, 80.0});
    model.insert(std::move(node));

    expect(model.positionOf("card-1").x == 10.0, "initial x");
    expect(model.positionOf("card-1").y == 20.0, "initial y");

    stack.pushAndExecute(std::make_unique<MoveNodeCommand>(
        model, "card-1", FluidCore::Point{10.0, 20.0}, FluidCore::Point{80.0, 90.0}));

    expect(model.positionOf("card-1").x == 80.0, "moved x");
    expect(model.positionOf("card-1").y == 90.0, "moved y");

    stack.undo();
    expect(model.positionOf("card-1").x == 10.0, "undone x");
    expect(model.positionOf("card-1").y == 20.0, "undone y");

    stack.redo();
    expect(model.positionOf("card-1").x == 80.0, "redone x");
    expect(model.positionOf("card-1").y == 90.0, "redone y");

    std::cout << "[PASS] testWorkspaceMoveNodeCommand\n";
}

void testUndoRedoByteTracking() {
    UndoStack stack(100, 1000); // Max bytes: 1000
    AnnotationStore store;

    Stroke stroke;
    stroke.color = 0x00FF00;
    stroke.width = 1.0;
    stroke.points = {{0.0, 0.0}, {10.0, 10.0}};

    auto cmd = std::make_unique<AddStrokeCommand>(store, 0, stroke);
    const std::size_t cmdSize = cmd->estimatedSizeBytes();
    expect(cmdSize > 0, "command size is non-zero");

    stack.pushAndExecute(std::move(cmd));
    expect(stack.estimatedSizeBytes() == cmdSize, "bytes recorded after push");

    stack.undo();
    expect(stack.estimatedSizeBytes() == 0, "bytes decremented to 0 after undo");

    stack.redo();
    expect(stack.estimatedSizeBytes() == cmdSize, "bytes restored after redo");

    stack.undo();
    expect(stack.estimatedSizeBytes() == 0, "bytes decremented to 0 after second undo");

    std::cout << "[PASS] testUndoRedoByteTracking\n";
}

void testAddStrokeAutoIdRedo() {
    AnnotationStore store;
    UndoStack stack;

    Stroke strokeWithoutId; // id is empty
    strokeWithoutId.points = {{5.0, 5.0}, {15.0, 15.0}};

    stack.pushAndExecute(std::make_unique<AddStrokeCommand>(store, 0, strokeWithoutId));
    expect(store.strokes().size() == 1, "store has 1 stroke");
    const std::string initialId = store.strokes()[0].id;
    expect(!initialId.empty(), "stroke ID was automatically generated");

    stack.undo();
    expect(store.strokes().empty(), "stroke removed on undo");

    stack.redo();
    expect(store.strokes().size() == 1, "stroke restored on redo");
    expect(store.strokes()[0].id == initialId, "stroke ID preserved after redo");

    stack.undo();
    expect(store.strokes().empty(), "stroke successfully removed again on second undo");

    std::cout << "[PASS] testAddStrokeAutoIdRedo\n";
}

int main() {
    testBasicUndoRedo();
    testRedoTruncationOnNewCommand();
    testMaxDepthCapacityTrimming();
    testCompoundCommand();
    testAnnotationCommands();
    testWorkspaceMoveNodeCommand();
    testUndoRedoByteTracking();
    testAddStrokeAutoIdRedo();
    std::cout << "All UndoStack tests passed successfully!\n";
    return 0;
}
