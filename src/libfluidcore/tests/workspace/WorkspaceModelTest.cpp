// Headless unit tests for WorkspaceModel (mirrors workspace/WorkspaceModel.cpp).

#include "workspace/WorkspaceModel.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace FluidCore;

class RectNode final : public WorkspaceNode {
  public:
    RectNode(std::string id, Rectangle bounds) : m_id(std::move(id)), m_bounds(bounds) {}
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

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

bool close(double a, double b) {
    const double eps = 1e-9;
    return a - eps <= b && b <= a + eps;
}

bool rectClose(const Rectangle& a, const Rectangle& b) {
    return close(a.x, b.x) && close(a.y, b.y) && close(a.w, b.w) && close(a.h, b.h);
}

int testInsertContract() {
    int failures = 0;
    WorkspaceModel model("proj-1");
    failures += check(model.projectId() == "proj-1", "model keeps its project id");

    failures += check(model.insert(nullptr).empty(), "null node insert rejected");
    failures += check(model.insert(std::make_unique<RectNode>("", Rectangle{0, 0, 5, 5})).empty(),
                      "empty-id node insert rejected");

    const std::string id =
        model.insert(std::make_unique<RectNode>("node-a", Rectangle{10.0, 20.0, 30.0, 40.0}));
    failures += check(id == "node-a", "insert reports the node's own id");
    failures += check(model.nodeCount() == 1, "insert registers the node");
    failures +=
        check(model.insert(std::make_unique<RectNode>("node-a", Rectangle{1, 1, 2, 2})).empty(),
              "duplicate id insert rejected");
    failures += check(model.nodeCount() == 1, "rejected insert leaves the model unchanged");

    failures += check(model.find("node-a") != nullptr, "find returns a registered node");
    failures += check(model.find("missing") == nullptr, "find misses unknown ids");
    failures += check(rectClose(model.boundsOf("node-a"), {10.0, 20.0, 30.0, 40.0}),
                      "bounds round-trip through insert");
    const Point p = model.positionOf("node-a");
    failures += check(close(p.x, 10.0) && close(p.y, 20.0), "position matches insert origin");
    return failures;
}

int testMoveAndVisibility() {
    int failures = 0;
    WorkspaceModel model("proj-1");
    model.insert(std::make_unique<RectNode>("a", Rectangle{0.0, 0.0, 20.0, 20.0}));
    model.insert(std::make_unique<RectNode>("b", Rectangle{500.0, 500.0, 50.0, 50.0}));

    auto visibleIn = [&model](const Rectangle& viewport) {
        std::vector<std::string> ids;
        for (const WorkspaceNode* node : model.visibleIn(viewport))
            ids.push_back(node->id());
        return ids;
    };

    failures += check(visibleIn({-10.0, -10.0, 100.0, 100.0}).size() == 1 &&
                          visibleIn({-10.0, -10.0, 100.0, 100.0})[0] == "a",
                      "left viewport sees only A");
    failures +=
        check(visibleIn({400.0, 400.0, 300.0, 300.0}).size() == 1, "right viewport sees only B");

    failures += check(!model.move("missing", 1.0, 1.0), "moving an unknown node fails");
    failures += check(model.move("b", 5.0, 5.0), "move succeeds for known nodes");
    const Point p = model.positionOf("b");
    failures += check(close(p.x, 5.0) && close(p.y, 5.0), "move repositions the node");
    failures +=
        check(rectClose(model.boundsOf("b"), {5.0, 5.0, 50.0, 50.0}), "move preserves node size");
    failures += check(visibleIn({0.0, 0.0, 60.0, 60.0}).size() == 2,
                      "viewport at destination sees both nodes after move");
    failures += check(visibleIn({400.0, 400.0, 300.0, 300.0}).empty(),
                      "old region no longer returns the moved node");
    return failures;
}

int testRemove() {
    int failures = 0;
    WorkspaceModel model("proj-1");
    model.insert(std::make_unique<RectNode>("a", Rectangle{0.0, 0.0, 20.0, 20.0}));
    model.insert(std::make_unique<RectNode>("b", Rectangle{10.0, 10.0, 40.0, 40.0}));

    failures += check(!model.remove("missing"), "removing an unknown node fails");
    failures += check(model.remove("a"), "remove succeeds for known nodes");
    failures +=
        check(model.nodeCount() == 1 && model.find("a") == nullptr, "removed node is unregistered");
    failures += check(model.boundsOf("a").w == 0.0 && model.boundsOf("a").h == 0.0,
                      "bounds of unknown nodes are zeroed");

    bool sawB = false;
    for (const WorkspaceNode* node : model.visibleIn({0.0, 0.0, 60.0, 60.0})) {
        if (node->id() == "a")
            sawB = true;
    }
    failures += check(!sawB, "removed node leaves visibility queries");
    failures += check(model.visibleIn({0.0, 0.0, 60.0, 60.0}).size() == 1,
                      "surviving node still visible in overlapping viewport");
    return failures;
}

int testGlobalBounds() {
    int failures = 0;
    WorkspaceModel model("proj-1");
    failures += check(rectClose(model.globalBounds(), {0.0, 0.0, 0.0, 0.0}),
                      "empty model globalBounds is zero rectangle");

    model.insert(std::make_unique<RectNode>("a", Rectangle{10.0, 20.0, 30.0, 40.0}));
    failures += check(rectClose(model.globalBounds(), {10.0, 20.0, 30.0, 40.0}),
                      "single node globalBounds matches its bounding box");

    model.insert(std::make_unique<RectNode>("b", Rectangle{100.0, 200.0, 50.0, 60.0}));
    // minX=10, minY=20, maxX=150, maxY=260 => width=140, height=240
    failures += check(rectClose(model.globalBounds(), {10.0, 20.0, 140.0, 240.0}),
                      "two nodes globalBounds computes union bounding box");

    failures += check(model.allNodeIds().size() == 2, "allNodeIds returns all registered IDs");

    model.remove("b");
    failures += check(rectClose(model.globalBounds(), {10.0, 20.0, 30.0, 40.0}),
                      "removing node updates globalBounds");
    return failures;
}

int testClearContract() {
    int failures = 0;
    WorkspaceModel model("proj-clear");
    model.insert(std::make_unique<RectNode>("node-1", Rectangle{10.0, 10.0, 50.0, 50.0}));
    model.insert(std::make_unique<RectNode>("node-2", Rectangle{100.0, 100.0, 50.0, 50.0}));
    failures += check(model.nodeCount() == 2, "nodes inserted before clear");

    model.clear();
    failures += check(model.nodeCount() == 0, "clear resets node count to zero");
    failures += check(model.allNodeIds().empty(), "clear empties allNodeIds");
    failures += check(model.visibleIn({-1000.0, -1000.0, 2000.0, 2000.0}).empty(),
                      "clear clears spatial query index");
    failures += check(rectClose(model.globalBounds(), {0.0, 0.0, 0.0, 0.0}),
                      "clear resets globalBounds to zero");

    // Insert again after clear to verify index continues to work
    model.insert(std::make_unique<RectNode>("node-3", Rectangle{20.0, 30.0, 40.0, 50.0}));
    failures += check(model.nodeCount() == 1, "model accepts nodes after clear");
    failures += check(model.find("node-3") != nullptr, "new node found after clear");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += testInsertContract();
    failures += testMoveAndVisibility();
    failures += testRemove();
    failures += testGlobalBounds();
    failures += testClearContract();

    if (failures == 0) {
        std::cout << "WorkspaceModelTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
