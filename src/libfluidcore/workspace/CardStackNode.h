#pragma once

#include "FluidCoreAPI.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace FluidCore {

// Hierarchical accordion container managing an ordered collection of excerpt cards
// (specs/new-features-backlog.md §2.2, TRD §3.4, PRD FR-4.2).
// Pure C++20 domain model with zero GTK/Cairo dependencies (ADR-0001).
class CardStackNode final : public WorkspaceNode {
  public:
    static constexpr size_t kMaxNestingDepth = 5;
    static constexpr double kHeaderHeight = 32.0;
    static constexpr double kCascadeTabOffset = 36.0;
    static constexpr double kCollapsedSummaryHeight = 44.0;

    explicit CardStackNode(std::string id, Rectangle bounds = {0.0, 0.0, 240.0, 160.0},
                           std::string title = "", bool isCollapsed = false);

    ~CardStackNode() override = default;

    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return m_bounds; }
    std::unique_ptr<WorkspaceNode> clone() const override;

    const std::string& title() const { return m_title; }
    void setTitle(std::string title) {
        m_title = std::move(title);
        m_customTitle = true;
    }

    bool hasCustomTitle() const { return m_customTitle; }
    void setCustomTitle(bool custom) { m_customTitle = custom; }

    bool isCollapsed() const { return m_isCollapsed; }
    void setCollapsed(bool collapsed);
    void toggleCollapsed();

    // Children management
    const std::vector<std::unique_ptr<WorkspaceNode>>& children() const { return m_children; }
    size_t childCount() const { return m_children.size(); }
    bool empty() const { return m_children.empty(); }

    // Adds a child at the end of the stack. Enforces max depth constraint.
    // If child is a stack whose depth would exceed kMaxNestingDepth, flattens its children.
    bool addChild(std::unique_ptr<WorkspaceNode> child);

    // Inserts child at specified index
    bool insertChild(size_t index, std::unique_ptr<WorkspaceNode> child);

    // Removes child by ID and returns ownership. Recalculates layout.
    std::unique_ptr<WorkspaceNode> removeChild(const std::string& childId);

    // Direct lookup among immediate children
    WorkspaceNode* findChild(const std::string& childId) const;

    // Recursive lookup through all nested stacks
    WorkspaceNode* findChildRecursive(const std::string& childId) const;

    bool containsChild(const std::string& childId) const;

    // Nesting depth calculation (1 for single-level stack, up to 5)
    size_t nestingDepth() const;
    bool canNest(size_t incomingDepth) const;

    // Position & Bounds manipulation
    void setBounds(const Rectangle& bounds) { m_bounds = bounds; }
    void setPosition(double x, double y) override;
    void translate(double dx, double dy);

    // Recomputes composite bounds and child card positions
    void recalculateLayout();

  private:
    std::string m_id;
    std::string m_title;
    bool m_customTitle = false;
    bool m_isCollapsed = false;
    Rectangle m_bounds;
    std::vector<std::unique_ptr<WorkspaceNode>> m_children;

    void updateAutoTitle();
};

} // namespace FluidCore
