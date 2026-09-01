#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"

#include <algorithm>
#include <utility>

namespace FluidCore {

CardStackNode::CardStackNode(std::string id, Rectangle bounds, std::string title, bool isCollapsed)
    : m_id(std::move(id)), m_isCollapsed(isCollapsed), m_bounds(bounds) {
    if (!title.empty()) {
        m_title = std::move(title);
        m_customTitle = true;
    } else {
        m_title = "Topic Stack";
        m_customTitle = false;
    }
}

std::unique_ptr<WorkspaceNode> CardStackNode::clone() const {
    auto copy = std::make_unique<CardStackNode>(m_id, m_bounds, m_title, m_isCollapsed);
    copy->m_customTitle = m_customTitle;
    copy->m_tags = m_tags;
    copy->m_children.reserve(m_children.size());
    for (const auto& child : m_children) {
        if (child) {
            copy->m_children.push_back(child->clone());
        }
    }
    return copy;
}

void CardStackNode::addTag(std::string tag) {
    if (tag.empty()) {
        return;
    }
    if (!hasTag(tag)) {
        m_tags.push_back(std::move(tag));
    }
}

bool CardStackNode::hasTag(const std::string& tag) const {
    return std::find(m_tags.begin(), m_tags.end(), tag) != m_tags.end();
}

void CardStackNode::setCollapsed(bool collapsed) {
    if (m_isCollapsed != collapsed) {
        m_isCollapsed = collapsed;
        recalculateLayout();
    }
}

void CardStackNode::toggleCollapsed() {
    setCollapsed(!m_isCollapsed);
}

size_t CardStackNode::nestingDepth() const {
    size_t depth = 1;
    for (const auto& child : m_children) {
        if (const auto* stackChild = dynamic_cast<const CardStackNode*>(child.get())) {
            depth = std::max(depth, 1 + stackChild->nestingDepth());
        }
    }
    return depth;
}

bool CardStackNode::canNest(size_t incomingDepth) const {
    return (nestingDepth() + incomingDepth) <= kMaxNestingDepth;
}

bool CardStackNode::addChild(std::unique_ptr<WorkspaceNode> child) {
    if (!child) {
        return false;
    }

    if (auto* incomingStack = dynamic_cast<CardStackNode*>(child.get())) {
        if (canNest(incomingStack->nestingDepth())) {
            m_children.push_back(std::move(child));
        } else {
            // Flatten: absorb incoming stack's children directly to avoid violating depth-5
            std::vector<std::unique_ptr<WorkspaceNode>> extracted;
            while (!incomingStack->m_children.empty()) {
                extracted.push_back(std::move(incomingStack->m_children.front()));
                incomingStack->m_children.erase(incomingStack->m_children.begin());
            }
            for (auto& item : extracted) {
                m_children.push_back(std::move(item));
            }
        }
    } else {
        m_children.push_back(std::move(child));
    }

    updateAutoTitle();
    recalculateLayout();
    return true;
}

bool CardStackNode::insertChild(size_t index, std::unique_ptr<WorkspaceNode> child) {
    if (!child) {
        return false;
    }

    if (index > m_children.size()) {
        index = m_children.size();
    }

    m_children.insert(m_children.begin() + index, std::move(child));
    updateAutoTitle();
    recalculateLayout();
    return true;
}

std::unique_ptr<WorkspaceNode> CardStackNode::removeChild(const std::string& childId) {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if ((*it)->id() == childId) {
            std::unique_ptr<WorkspaceNode> removed = std::move(*it);
            m_children.erase(it);
            updateAutoTitle();
            recalculateLayout();
            return removed;
        }
    }

    // Check recursive children
    for (auto& child : m_children) {
        if (auto* stackChild = dynamic_cast<CardStackNode*>(child.get())) {
            auto removed = stackChild->removeChild(childId);
            if (removed) {
                recalculateLayout();
                return removed;
            }
        }
    }

    return nullptr;
}

WorkspaceNode* CardStackNode::findChild(const std::string& childId) const {
    for (const auto& child : m_children) {
        if (child && child->id() == childId) {
            return child.get();
        }
    }
    return nullptr;
}

WorkspaceNode* CardStackNode::findChildRecursive(const std::string& childId) const {
    for (const auto& child : m_children) {
        if (!child)
            continue;
        if (child->id() == childId) {
            return child.get();
        }
        if (const auto* stackChild = dynamic_cast<const CardStackNode*>(child.get())) {
            auto* found = stackChild->findChildRecursive(childId);
            if (found) {
                return found;
            }
        }
    }
    return nullptr;
}

bool CardStackNode::containsChild(const std::string& childId) const {
    return findChildRecursive(childId) != nullptr;
}

void CardStackNode::translate(double dx, double dy) {
    m_bounds.x += dx;
    m_bounds.y += dy;

    for (auto& child : m_children) {
        if (!child)
            continue;
        if (auto* excerpt = dynamic_cast<ExcerptCardNode*>(child.get())) {
            const auto b = excerpt->bounds();
            excerpt->setPosition(b.x + dx, b.y + dy);
        } else if (auto* stack = dynamic_cast<CardStackNode*>(child.get())) {
            stack->translate(dx, dy);
        }
    }
}

void CardStackNode::setPosition(double x, double y) {
    const double dx = x - m_bounds.x;
    const double dy = y - m_bounds.y;
    translate(dx, dy);
}

void CardStackNode::recalculateLayout() {
    double stackW = 240.0;
    for (const auto& child : m_children) {
        if (child) {
            stackW = std::max(stackW, child->bounds().w);
        }
    }
    m_bounds.w = stackW;

    if (!m_isCollapsed) {
        double currentY = m_bounds.y + kHeaderHeight;
        for (size_t i = 0; i < m_children.size(); ++i) {
            auto& child = m_children[i];
            if (!child)
                continue;

            if (auto* excerpt = dynamic_cast<ExcerptCardNode*>(child.get())) {
                const double cardH = excerpt->bounds().h > 0.0 ? excerpt->bounds().h : 160.0;
                excerpt->setBounds(Rectangle{m_bounds.x, currentY, stackW, cardH});
                currentY += kCascadeTabOffset;
            } else if (auto* stack = dynamic_cast<CardStackNode*>(child.get())) {
                stack->setPosition(m_bounds.x + 8.0, currentY);
                stack->recalculateLayout();
                currentY += kCascadeTabOffset;
            }
        }

        if (!m_children.empty()) {
            const auto& lastChild = m_children.back();
            m_bounds.h = (lastChild->bounds().y + lastChild->bounds().h) - m_bounds.y;
        } else {
            m_bounds.h = kHeaderHeight + 120.0;
        }
    } else {
        m_bounds.h = kCollapsedSummaryHeight;
        const double summaryChildY = m_bounds.y + kHeaderHeight;
        for (auto& child : m_children) {
            if (!child)
                continue;
            if (auto* excerpt = dynamic_cast<ExcerptCardNode*>(child.get())) {
                const double cardH = excerpt->bounds().h > 0.0 ? excerpt->bounds().h : 160.0;
                excerpt->setBounds(Rectangle{m_bounds.x, summaryChildY, stackW, cardH});
            } else if (auto* stack = dynamic_cast<CardStackNode*>(child.get())) {
                stack->setPosition(m_bounds.x, summaryChildY);
            }
        }
    }
}

void CardStackNode::updateAutoTitle() {
    if (!m_customTitle) {
        if (m_children.size() == 1) {
            m_title = "Topic Stack (1 item)";
        } else {
            m_title = "Topic Stack (" + std::to_string(m_children.size()) + " items)";
        }
    }
}

} // namespace FluidCore
