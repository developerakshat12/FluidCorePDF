#include "workspace/CardStackNode.h"
#include "undo/UndoStack.h"
#include "undo/WorkspaceCommands.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/WorkspaceModel.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace FluidCore;

std::unique_ptr<ExcerptCardNode> makeCard(const std::string& id, double x, double y,
                                          double w = 240.0, double h = 160.0) {
    return std::make_unique<ExcerptCardNode>(id, Rectangle{x, y, w, h}, "doc.pdf", 0,
                                             Rectangle{0.0, 0.0, 1.0, 1.0}, "Snippet " + id);
}

void testStackCreationAndLayout() {
    auto stack = std::make_unique<CardStackNode>("stack-1", Rectangle{100.0, 100.0, 240.0, 160.0});
    assert(stack->id() == "stack-1");
    assert(!stack->isCollapsed());
    assert(stack->childCount() == 0);

    // Add 2 child cards
    stack->addChild(makeCard("card-1", 100.0, 100.0));
    stack->addChild(makeCard("card-2", 100.0, 100.0));
    assert(stack->childCount() == 2);

    // In expanded mode:
    // Header = 32pt
    // Child 1 starts at y = 100 + 32 = 132, h = 160
    // Child 2 starts at y = 100 + 32 + 36 = 168, h = 160 -> bottom = 168 + 160 = 328
    // Total stack height = 328 - 100 = 228
    Rectangle bExpanded = stack->bounds();
    assert(std::abs(bExpanded.x - 100.0) < 1e-6);
    assert(std::abs(bExpanded.y - 100.0) < 1e-6);
    assert(std::abs(bExpanded.h - 228.0) < 1e-6);

    auto* c1 = stack->findChild("card-1");
    auto* c2 = stack->findChild("card-2");
    assert(c1 != nullptr && c2 != nullptr);
    assert(std::abs(c1->bounds().y - 132.0) < 1e-6);
    assert(std::abs(c2->bounds().y - 168.0) < 1e-6);

    // Collapse stack
    stack->setCollapsed(true);
    assert(stack->isCollapsed());
    Rectangle bCollapsed = stack->bounds();
    assert(std::abs(bCollapsed.h - CardStackNode::kCollapsedSummaryHeight) < 1e-6);

    // Expand again
    stack->toggleCollapsed();
    assert(!stack->isCollapsed());
    assert(std::abs(stack->bounds().h - 228.0) < 1e-6);

    std::cout << "[PASS] testStackCreationAndLayout\n";
}

void testCompoundTranslation() {
    auto stack = std::make_unique<CardStackNode>("stack-1", Rectangle{100.0, 100.0, 240.0, 160.0});
    stack->addChild(makeCard("card-1", 100.0, 100.0));
    stack->addChild(makeCard("card-2", 100.0, 100.0));

    // Move stack by (+50, +30)
    stack->translate(50.0, 30.0);
    assert(std::abs(stack->bounds().x - 150.0) < 1e-6);
    assert(std::abs(stack->bounds().y - 130.0) < 1e-6);

    auto* c1 = stack->findChild("card-1");
    auto* c2 = stack->findChild("card-2");
    assert(std::abs(c1->bounds().x - 150.0) < 1e-6);
    assert(std::abs(c1->bounds().y - (132.0 + 30.0)) < 1e-6);
    assert(std::abs(c2->bounds().x - 150.0) < 1e-6);
    assert(std::abs(c2->bounds().y - (168.0 + 30.0)) < 1e-6);

    std::cout << "[PASS] testCompoundTranslation\n";
}

void testNestingDepthAndFlattening() {
    // Level 1 stack
    auto stack1 = std::make_unique<CardStackNode>("stack-1");
    stack1->addChild(makeCard("card-1", 0.0, 0.0));
    assert(stack1->nestingDepth() == 1);

    // Nest stack2 inside stack1 (depth becomes 2)
    auto stack2 = std::make_unique<CardStackNode>("stack-2");
    stack2->addChild(makeCard("card-2", 0.0, 0.0));
    stack1->addChild(std::move(stack2));
    assert(stack1->nestingDepth() == 2);

    // Nest stack3 inside stack2 (depth becomes 3)
    auto stack3 = std::make_unique<CardStackNode>("stack-3");
    stack3->addChild(makeCard("card-3", 0.0, 0.0));
    auto* s2 = dynamic_cast<CardStackNode*>(stack1->findChild("stack-2"));
    assert(s2 != nullptr);
    s2->addChild(std::move(stack3));
    assert(stack1->nestingDepth() == 3);

    // Nest stack4 inside stack3 (depth becomes 4)
    auto stack4 = std::make_unique<CardStackNode>("stack-4");
    stack4->addChild(makeCard("card-4", 0.0, 0.0));
    auto* s3 = dynamic_cast<CardStackNode*>(stack1->findChildRecursive("stack-3"));
    assert(s3 != nullptr);
    s3->addChild(std::move(stack4));
    assert(stack1->nestingDepth() == 4);

    // Nest stack5 inside stack4 (depth becomes 5 - max depth)
    auto stack5 = std::make_unique<CardStackNode>("stack-5");
    stack5->addChild(makeCard("card-5", 0.0, 0.0));
    auto* s4 = dynamic_cast<CardStackNode*>(stack1->findChildRecursive("stack-4"));
    assert(s4 != nullptr);
    s4->addChild(std::move(stack5));
    assert(stack1->nestingDepth() == 5);

    // Attempting to nest stack6 inside stack5 would exceed depth 5 -> FLATTENS
    auto stack6 = std::make_unique<CardStackNode>("stack-6");
    stack6->addChild(makeCard("card-6a", 0.0, 0.0));
    stack6->addChild(makeCard("card-6b", 0.0, 0.0));

    auto* s5 = dynamic_cast<CardStackNode*>(stack1->findChildRecursive("stack-5"));
    assert(s5 != nullptr);
    assert(!s5->canNest(stack6->nestingDepth()));

    s5->addChild(std::move(stack6));
    // Depth remains capped at 5
    assert(stack1->nestingDepth() == 5);
    // Children card-6a and card-6b were flattened directly into s5
    assert(s5->findChild("card-6a") != nullptr);
    assert(s5->findChild("card-6b") != nullptr);

    std::cout << "[PASS] testNestingDepthAndFlattening\n";
}

void testWorkspaceModelStackIntegrationAndAutoDissolve() {
    WorkspaceModel model("proj-1");

    // Insert 2 standalone cards
    model.insert(makeCard("c1", 100.0, 100.0));
    model.insert(makeCard("c2", 100.0, 200.0));
    assert(model.nodeCount() == 2);

    // Merge c1 and c2 via StackMergeCommand
    StackMergeCommand mergeCmd(model, "c2", "c1", "stack-main");
    bool ok = mergeCmd.execute();
    assert(ok);
    assert(model.nodeCount() == 1);
    assert(model.find("stack-main") != nullptr);

    // Child bounds resolution in expanded vs collapsed
    Rectangle bC1 = model.boundsOf("c1");
    assert(bC1.w > 0.0);

    auto* stack = dynamic_cast<CardStackNode*>(model.find("stack-main"));
    assert(stack != nullptr);
    stack->setCollapsed(true);
    Rectangle bC1Collapsed = model.boundsOf("c1");
    assert(std::abs(bC1Collapsed.h - CardStackNode::kCollapsedSummaryHeight) < 1e-6);

    stack->setCollapsed(false);

    // Extract c2 from stack
    ExtractChildCommand extractCmd(model, "stack-main", "c2", Point{500.0, 500.0});
    bool extractOk = extractCmd.execute();
    assert(extractOk);

    // Since stack-main only had 2 items and 1 was extracted, it auto-dissolved!
    // The remaining child c1 is now a top-level node.
    assert(model.find("stack-main") == nullptr);
    assert(model.find("c1") != nullptr);
    assert(model.find("c2") != nullptr);
    assert(model.nodeCount() == 2);

    // Test Undo of extraction restores stack
    extractCmd.undo();
    assert(model.nodeCount() == 1);
    assert(model.find("stack-main") != nullptr);

    // Test Undo of merge restores original 2 individual cards
    mergeCmd.undo();
    assert(model.nodeCount() == 2);
    assert(model.find("c1") != nullptr);
    assert(model.find("c2") != nullptr);

    std::cout << "[PASS] testWorkspaceModelStackIntegrationAndAutoDissolve\n";
}

void testToggleStackCollapseCommand() {
    WorkspaceModel model("proj-1");
    auto stack = std::make_unique<CardStackNode>("stack-toggle");
    stack->addChild(makeCard("c1", 0.0, 0.0));
    stack->addChild(makeCard("c2", 0.0, 0.0));
    model.insert(std::move(stack));

    ToggleStackCollapseCommand cmd(model, "stack-toggle", true);
    cmd.execute();

    auto* s = dynamic_cast<CardStackNode*>(model.find("stack-toggle"));
    assert(s != nullptr && s->isCollapsed());

    cmd.undo();
    assert(!s->isCollapsed());

    cmd.redo();
    assert(s->isCollapsed());

    std::cout << "[PASS] testToggleStackCollapseCommand\n";
}

int main() {
    std::cout << "Running CardStackNodeTest...\n";
    testStackCreationAndLayout();
    testCompoundTranslation();
    testNestingDepthAndFlattening();
    testWorkspaceModelStackIntegrationAndAutoDissolve();
    testToggleStackCollapseCommand();
    std::cout << "All CardStackNodeTest cases passed!\n";
    return 0;
}
