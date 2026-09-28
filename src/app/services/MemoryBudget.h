#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace FluidCoreApp {

// Single authority for every byte-budgeted consumer in the app.
//
// Before this existed, four independent caches each declared kDefaultMaxBytes = 64 MB
// and each enforced it only against its own surfaces:
//
//   PageTileCache    64 MB
//   ExcerptTileCache 64 MB
//   UndoStack (pane) 64 MB
//   UndoStack (view) 64 MB
//   ---------------------------
//   authorized      256 MB
//
// Nothing arbitrated between them and nothing ever called setMaxBytes(), so a session
// could legitimately consume 256 MB of headroom against a documented idle target of
// 120 MB (TRD.md §4). The budgets were also not connected to physical RAM.
//
// This class partitions one total into named slices, hands each slice to its owner, and
// can shrink or restore the whole pool when the app goes idle. Slices are advisory: each
// consumer still enforces its own byte accounting, this only decides how much each is
// allowed.
class MemoryBudget {
  public:
    enum class Slice { PageTiles, ExcerptTiles, UndoStack, Count };

    static constexpr std::size_t kDefaultTotalBytes = 64 * 1024 * 1024;

    // Fraction of the total that survives when the app is idle. Idle trim releases
    // cache surfaces back to the allocator instead of holding them until exit.
    static constexpr double kIdleRetainedFraction = 0.25;

    static MemoryBudget& instance();

    // Derives the total from physical RAM, clamped to [kMinTotal, kMaxTotal]. Falls back
    // to kDefaultTotalBytes when RAM cannot be determined.
    static std::size_t recommendedTotalBytes();
    static std::size_t physicalMemoryBytes();

    // Slice size in bytes. A slice never exceeds the current total.
    std::size_t sliceBytes(Slice slice) const;

    // There are two independent UndoStacks (document pane and workspace canvas), so the
    // UndoStack slice is split evenly between them. UndoStack lives in libfluidcore and
    // must not depend on this class (ADR-0001), so the GUI sets each stack's byte budget
    // explicitly at construction rather than the engine reading the pool itself.
    std::size_t undoBytesPerStack() const { return sliceBytes(Slice::UndoStack) / 2; }

    void setSliceBytes(Slice slice, std::size_t bytes);

    // Lowers every slice to kIdleRetainedFraction of its current size. Safe to call
    // repeatedly; consumers are expected to evict down to their new limits.
    void trimToIdle();

    // Restores slices to their full configured size.
    void restoreFromIdle();

    bool isIdle() const { return m_idle; }

    std::size_t totalBytes() const { return m_totalBytes; }
    void setTotalBytes(std::size_t bytes);

    // Diagnostic string for the telemetry heartbeat.
    std::string describe() const;

    static const char* sliceName(Slice slice);

  private:
    MemoryBudget() = default;

    static constexpr std::size_t kMinTotalBytes = 16 * 1024 * 1024;
    static constexpr std::size_t kMaxTotalBytes = 512 * 1024 * 1024;

    // Full (non-trimmed) allocation per slice. The partition is deliberately
    // page-tile heavy: page tiles back the reading experience the user is looking at
    // right now, excerpt tiles are card thumbnails that re-render cheaply, and undo
    // history is depth-bounded in practice so its byte ceiling is mostly headroom.
    std::size_t m_fullBytes[static_cast<std::size_t>(Slice::Count)] = {};
    std::size_t m_idleBytes[static_cast<std::size_t>(Slice::Count)] = {};
    std::size_t m_totalBytes = kDefaultTotalBytes;
    bool m_idle = false;
    bool m_initialized = false;

    void ensureInitialized();
};

} // namespace FluidCoreApp
