#include "RTreeIndex.h"

#include <algorithm>
#include <utility>

namespace FluidCore {
namespace {

double area(const Rectangle& r) {
    return r.w * r.h;
}

bool intersects(const Rectangle& a, const Rectangle& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

Rectangle unionOf(const Rectangle& a, const Rectangle& b) {
    if (a.w <= 0 || a.h <= 0)
        return b;
    if (b.w <= 0 || b.h <= 0)
        return a;
    const double minX = std::min(a.x, b.x);
    const double minY = std::min(a.y, b.y);
    const double maxX = std::max(a.x + a.w, b.x + b.w);
    const double maxY = std::max(a.y + a.h, b.y + b.h);
    return {minX, minY, maxX - minX, maxY - minY};
}

double enlargement(const Rectangle& target, const Rectangle& extra) {
    return area(unionOf(target, extra)) - area(target);
}

} // namespace

RTreeIndex::Handle RTreeIndex::insert(const Rectangle& bounds) {
    const Handle handle = m_nextHandle++;
    Entry entry;
    entry.bounds = bounds;
    entry.handle = handle;
    insertRooted(std::move(entry));
    m_bounds.emplace(handle, bounds);
    return handle;
}

bool RTreeIndex::remove(Handle handle) {
    const auto it = m_bounds.find(handle);
    if (it == m_bounds.end())
        return false;

    eraseFrom(m_root, handle, it->second);
    m_bounds.erase(it);

    while (m_root != kNoneNode && !m_nodes[m_root].leaf && m_nodes[m_root].entries.size() == 1) {
        const std::uint32_t oldRoot = m_root;
        m_root = m_nodes[oldRoot].entries.front().child;
        m_nodes[oldRoot].entries.clear();
        m_freeNodes.push_back(oldRoot);
    }
    return true;
}

void RTreeIndex::update(Handle handle, const Rectangle& bounds) {
    const auto it = m_bounds.find(handle);
    if (it == m_bounds.end())
        return;

    const Rectangle previous = it->second;
    it->second = bounds;
    eraseFrom(m_root, handle, previous);

    Entry entry;
    entry.bounds = bounds;
    entry.handle = handle;
    insertRooted(std::move(entry));
}

std::vector<RTreeIndex::Handle> RTreeIndex::query(const Rectangle& region) const {
    std::vector<Handle> out;
    if (m_root != kNoneNode)
        collect(m_root, region, out);
    return out;
}

std::uint32_t RTreeIndex::allocNode(bool leaf) {
    if (!m_freeNodes.empty()) {
        const std::uint32_t idx = m_freeNodes.back();
        m_freeNodes.pop_back();
        m_nodes[idx] = Node{};
        m_nodes[idx].leaf = leaf;
        return idx;
    }
    m_nodes.push_back(Node{});
    m_nodes.back().leaf = leaf;
    return static_cast<std::uint32_t>(m_nodes.size() - 1);
}

void RTreeIndex::insertRooted(Entry entry) {
    if (m_root == kNoneNode)
        m_root = allocNode(true);

    const std::uint32_t sibling = insertEntry(m_root, std::move(entry));
    if (sibling == kNoneNode)
        return;

    const std::uint32_t newRoot = allocNode(false);
    Entry left;
    left.child = m_root;
    left.bounds = m_nodes[m_root].bounds;
    Entry right;
    right.child = sibling;
    right.bounds = m_nodes[sibling].bounds;
    auto& rootEntries = m_nodes[newRoot].entries;
    rootEntries.push_back(left);
    rootEntries.push_back(right);
    tightenUp(newRoot);
    m_root = newRoot;
}

std::uint32_t RTreeIndex::insertEntry(std::uint32_t nodeIdx, Entry entry) {
    if (m_nodes[nodeIdx].leaf) {
        m_nodes[nodeIdx].entries.push_back(std::move(entry));
        if (m_nodes[nodeIdx].entries.size() > kMaxEntries)
            return splitNode(nodeIdx);
        tightenUp(nodeIdx);
        return kNoneNode;
    }

    const std::uint32_t childIdx = chooseSubtree(nodeIdx, entry.bounds);
    const std::uint32_t sibling = insertEntry(childIdx, std::move(entry));
    tightenUp(nodeIdx);
    if (sibling == kNoneNode)
        return kNoneNode;

    Entry up;
    up.child = sibling;
    up.bounds = m_nodes[sibling].bounds;
    m_nodes[nodeIdx].entries.push_back(std::move(up));
    if (m_nodes[nodeIdx].entries.size() > kMaxEntries)
        return splitNode(nodeIdx);
    return kNoneNode;
}

std::uint32_t RTreeIndex::splitNode(std::uint32_t nodeIdx) {
    const bool leaf = m_nodes[nodeIdx].leaf;
    std::vector<Entry> pool = std::move(m_nodes[nodeIdx].entries);
    m_nodes[nodeIdx].entries.clear();

    // Quadratic pick of the two seeds that waste the most area together.
    std::size_t seedA = 0;
    std::size_t seedB = 1;
    double worstWaste = -1.0;
    for (std::size_t i = 0; i < pool.size(); ++i) {
        for (std::size_t j = i + 1; j < pool.size(); ++j) {
            const double waste = area(unionOf(pool[i].bounds, pool[j].bounds)) -
                                 area(pool[i].bounds) - area(pool[j].bounds);
            if (waste > worstWaste) {
                worstWaste = waste;
                seedA = i;
                seedB = j;
            }
        }
    }

    const std::uint32_t siblingIdx = allocNode(leaf);
    m_nodes[nodeIdx].entries.push_back(std::move(pool[seedA]));
    m_nodes[siblingIdx].entries.push_back(std::move(pool[seedB]));
    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(seedB));
    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(seedA));

    Node& primary = m_nodes[nodeIdx];
    Node& sibling = m_nodes[siblingIdx];
    while (!pool.empty()) {
        // Once one group is full-ish, the rest must go to the other (keeps min fill).
        if (primary.entries.size() + pool.size() <= kMinEntries) {
            for (auto& e : pool)
                primary.entries.push_back(std::move(e));
            break;
        }
        if (sibling.entries.size() + pool.size() <= kMinEntries) {
            for (auto& e : pool)
                sibling.entries.push_back(std::move(e));
            break;
        }

        const Entry next = pool.back();
        pool.pop_back();

        // Standard quadratic-split pickNext: prefer the group needing least
        // enlargement; tie-break on smaller area, then fewer entries.
        Rectangle primaryBounds = primary.entries.front().bounds;
        Rectangle siblingBounds = sibling.entries.front().bounds;
        for (const auto& e : primary.entries)
            primaryBounds = unionOf(primaryBounds, e.bounds);
        for (const auto& e : sibling.entries)
            siblingBounds = unionOf(siblingBounds, e.bounds);

        bool toSibling = false;
        const double growPrimary = enlargement(primaryBounds, next.bounds);
        const double growSibling = enlargement(siblingBounds, next.bounds);
        if (growSibling < growPrimary) {
            toSibling = true;
        } else if (growSibling == growPrimary) {
            const double diffArea = area(siblingBounds) - area(primaryBounds);
            if (diffArea < 0) {
                toSibling = true;
            } else if (diffArea == 0) {
                toSibling = sibling.entries.size() < primary.entries.size();
            }
        }

        if (toSibling) {
            sibling.entries.push_back(std::move(next));
        } else {
            primary.entries.push_back(std::move(next));
        }
    }

    tightenUp(nodeIdx);
    tightenUp(siblingIdx);
    return siblingIdx;
}

std::uint32_t RTreeIndex::chooseSubtree(std::uint32_t nodeIdx, const Rectangle& bounds) const {
    const Node& node = m_nodes[nodeIdx];
    std::uint32_t best = node.entries.front().child;
    double bestGrow = enlargement(node.entries.front().bounds, bounds);
    double bestArea = area(node.entries.front().bounds);
    for (std::size_t i = 1; i < node.entries.size(); ++i) {
        const Entry& candidate = node.entries[i];
        const double grow = enlargement(candidate.bounds, bounds);
        const double candidateArea = area(candidate.bounds);
        if (grow < bestGrow || (grow == bestGrow && candidateArea < bestArea)) {
            best = candidate.child;
            bestGrow = grow;
            bestArea = candidateArea;
        }
    }
    return best;
}

bool RTreeIndex::eraseFrom(std::uint32_t nodeIdx, Handle handle, const Rectangle& hint) {
    Node& node = m_nodes[nodeIdx];

    if (!intersects(node.bounds, hint))
        return false;

    if (node.leaf) {
        for (std::size_t i = 0; i < node.entries.size(); ++i) {
            if (node.entries[i].handle == handle) {
                node.entries.erase(node.entries.begin() + static_cast<std::ptrdiff_t>(i));
                tightenUp(nodeIdx);
                return true;
            }
        }
        return false;
    }

    for (std::size_t i = 0; i < node.entries.size(); ++i) {
        const std::uint32_t childIdx = node.entries[i].child;
        if (!eraseFrom(childIdx, handle, hint))
            continue;

        if (m_nodes[childIdx].entries.empty()) {
            node.entries.erase(node.entries.begin() + static_cast<std::ptrdiff_t>(i));
            m_freeNodes.push_back(childIdx);
        } else {
            node.entries[i].bounds = m_nodes[childIdx].bounds;
        }
        tightenUp(nodeIdx);
        return true;
    }
    return false;
}

void RTreeIndex::tightenUp(std::uint32_t nodeIdx) {
    Node& node = m_nodes[nodeIdx];
    if (node.entries.empty()) {
        node.bounds = Rectangle{};
        return;
    }
    if (!node.leaf) {
        for (Entry& e : node.entries)
            e.bounds = m_nodes[e.child].bounds;
    }
    Rectangle bounds = node.entries.front().bounds;
    for (const Entry& e : node.entries)
        bounds = unionOf(bounds, e.bounds);
    node.bounds = bounds;
}

void RTreeIndex::collect(std::uint32_t nodeIdx, const Rectangle& region,
                         std::vector<Handle>& out) const {
    const Node& node = m_nodes[nodeIdx];
    if (!intersects(node.bounds, region))
        return;

    for (const Entry& e : node.entries) {
        if (!intersects(e.bounds, region))
            continue;
        if (node.leaf) {
            out.push_back(e.handle);
        } else {
            collect(e.child, region, out);
        }
    }
}

} // namespace FluidCore
