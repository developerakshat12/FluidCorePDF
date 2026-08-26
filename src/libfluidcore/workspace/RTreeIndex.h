#pragma once

#include "FluidCoreAPI.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace FluidCore {

// Spatial AABB index backing WorkspaceModel. Wave-1 slice: dynamic R-tree with
// quadratic split, no underflow compaction. The R*-tree heuristics + bulk load
// behind the ROADMAP §5 query-p99 budget land in M3 (TRD §3.4).
class RTreeIndex {
  public:
    using Handle = std::uint64_t;

    static constexpr Handle kInvalidHandle = 0;

    // Inserts an AABB and returns a stable handle for it.
    Handle insert(const Rectangle& bounds);

    // Removes the entry; returns false if the handle is unknown.
    bool remove(Handle handle);

    // Relocates an entry in place (handle is preserved). Unknown handles are ignored.
    void update(Handle handle, const Rectangle& bounds);

    // Handles of entries whose AABB intersects `region` (touching edges do not count).
    std::vector<Handle> query(const Rectangle& region) const;

    std::size_t size() const { return m_bounds.size(); }
    bool empty() const { return m_bounds.empty(); }

  private:
    static constexpr std::uint32_t kMaxEntries = 8;
    static constexpr std::uint32_t kMinEntries = kMaxEntries / 2;
    static constexpr std::uint32_t kNoneNode = 0xFFFFFFFFu;

    struct Entry {
        Rectangle bounds;
        std::uint32_t child = kNoneNode;
        Handle handle = kInvalidHandle;
    };

    struct Node {
        bool leaf = true;
        std::vector<Entry> entries;
        Rectangle bounds;
    };

    std::uint32_t allocNode(bool leaf);
    void insertRooted(Entry entry);
    std::uint32_t insertEntry(std::uint32_t nodeIdx, Entry entry);
    std::uint32_t splitNode(std::uint32_t nodeIdx);
    std::uint32_t chooseSubtree(std::uint32_t nodeIdx, const Rectangle& bounds) const;
    bool eraseFrom(std::uint32_t nodeIdx, Handle handle, const Rectangle& hint);
    void tightenUp(std::uint32_t nodeIdx);
    void collect(std::uint32_t nodeIdx, const Rectangle& region, std::vector<Handle>& out) const;

    std::vector<Node> m_nodes;
    std::vector<std::uint32_t> m_freeNodes;
    std::uint32_t m_root = kNoneNode;
    Handle m_nextHandle = 1;
    std::unordered_map<Handle, Rectangle> m_bounds;
};

} // namespace FluidCore
