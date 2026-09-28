#include "services/MemoryBudget.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace FluidCoreApp {

namespace {

// 24 / 24 / 16 MB out of a 64 MB pool. Page tiles and excerpt tiles are equal because
// both scale with zoom, and undo gets a quarter because kMaxDepth=100 caps history
// long before this byte ceiling is reached in normal editing.
constexpr std::size_t kPageTileShare = 24 * 1024 * 1024;
constexpr std::size_t kExcerptTileShare = 24 * 1024 * 1024;
constexpr std::size_t kUndoShare = 16 * 1024 * 1024;

} // namespace

const char* MemoryBudget::sliceName(Slice slice) {
    switch (slice) {
    case Slice::PageTiles:
        return "PageTiles";
    case Slice::ExcerptTiles:
        return "ExcerptTiles";
    case Slice::UndoStack:
        return "UndoStack";
    default:
        return "Unknown";
    }
}

std::size_t MemoryBudget::physicalMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<std::size_t>(status.ullTotalPhys);
    }
#endif
    return 0;
}

std::size_t MemoryBudget::recommendedTotalBytes() {
    const std::size_t physical = physicalMemoryBytes();
    if (physical == 0) {
        return kDefaultTotalBytes;
    }
    // Scale DOWN only. Plenty of RAM is not a reason to hold more tiles: the cache exists
    // to make scrolling smooth, and past a point extra budget only keeps stale surfaces
    // resident. Scale up solely to avoid starving a small machine.
    if (physical / 4 < kDefaultTotalBytes) {
        return std::max<std::size_t>(physical / 4, kMinTotalBytes);
    }
    return kDefaultTotalBytes;
}

MemoryBudget& MemoryBudget::instance() {
    static MemoryBudget budget;
    budget.ensureInitialized();
    return budget;
}

void MemoryBudget::ensureInitialized() {
    if (m_initialized) {
        return;
    }
    m_totalBytes = recommendedTotalBytes();

    const std::size_t requested = kPageTileShare + kExcerptTileShare + kUndoShare;
    if (requested <= m_totalBytes) {
        m_fullBytes[static_cast<std::size_t>(Slice::PageTiles)] = kPageTileShare;
        m_fullBytes[static_cast<std::size_t>(Slice::ExcerptTiles)] = kExcerptTileShare;
        m_fullBytes[static_cast<std::size_t>(Slice::UndoStack)] = kUndoShare;
    } else {
        // Small machine: split proportionally rather than letting one slice starve.
        const double scale = static_cast<double>(m_totalBytes) / static_cast<double>(requested);
        m_fullBytes[static_cast<std::size_t>(Slice::PageTiles)] =
            static_cast<std::size_t>(static_cast<double>(kPageTileShare) * scale);
        m_fullBytes[static_cast<std::size_t>(Slice::ExcerptTiles)] =
            static_cast<std::size_t>(static_cast<double>(kExcerptTileShare) * scale);
        m_fullBytes[static_cast<std::size_t>(Slice::UndoStack)] =
            static_cast<std::size_t>(static_cast<double>(kUndoShare) * scale);
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(Slice::Count); ++i) {
        m_idleBytes[i] = std::max<std::size_t>(m_fullBytes[i] / 4, 1024 * 1024);
    }
    m_initialized = true;
}

std::size_t MemoryBudget::sliceBytes(Slice slice) const {
    const std::size_t i = static_cast<std::size_t>(slice);
    return m_idle ? m_idleBytes[i] : m_fullBytes[i];
}

void MemoryBudget::setSliceBytes(Slice slice, std::size_t bytes) {
    const std::size_t i = static_cast<std::size_t>(slice);
    m_fullBytes[i] = std::min(bytes, m_totalBytes);
    m_idleBytes[i] = std::max<std::size_t>(m_fullBytes[i] / 4, 1024 * 1024);
}

void MemoryBudget::setTotalBytes(std::size_t bytes) {
    m_totalBytes = std::clamp(bytes, kMinTotalBytes, kMaxTotalBytes);
    // Re-derive shares against the new ceiling, preserving their proportions.
    const std::size_t previousTotal = m_fullBytes[0] + m_fullBytes[1] + m_fullBytes[2];
    if (previousTotal == 0) {
        return;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Slice::Count); ++i) {
        const double share =
            static_cast<double>(m_fullBytes[i]) / static_cast<double>(previousTotal);
        m_fullBytes[i] = static_cast<std::size_t>(static_cast<double>(m_totalBytes) * share);
        m_idleBytes[i] = std::max<std::size_t>(m_fullBytes[i] / 4, 1024 * 1024);
    }
}

void MemoryBudget::trimToIdle() {
    m_idle = true;
}

void MemoryBudget::restoreFromIdle() {
    m_idle = false;
}

std::string MemoryBudget::describe() const {
    std::ostringstream oss;
    oss << "total " << (m_totalBytes / (1024 * 1024)) << "MB" << (m_idle ? " (idle)" : "");
    for (std::size_t i = 0; i < static_cast<std::size_t>(Slice::Count); ++i) {
        oss << " | " << sliceName(static_cast<Slice>(i)) << " "
            << (sliceBytes(static_cast<Slice>(i)) / (1024 * 1024)) << "MB";
    }
    return oss.str();
}

} // namespace FluidCoreApp
