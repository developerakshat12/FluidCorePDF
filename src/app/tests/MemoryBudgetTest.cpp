// Tests for the global cache-budget arbiter.
//
// The failure this guards against is specific and was shipped once: MemoryBudget
// exposes trimToIdle()/restoreFromIdle(), but caches enforce their own m_maxBytes and
// never consult the arbiter again. A trim that only flips a boolean relabels the
// telemetry heartbeat while every cache keeps its full ceiling, and - because nothing
// called restoreFromIdle() - the arbiter stays in the idle state permanently.

#include "services/MemoryBudget.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace FluidCoreApp;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

std::size_t mb(std::size_t bytes) {
    return bytes / (1024 * 1024);
}

void testDefaultPartitionSumsToTotal() {
    std::printf("testDefaultPartitionSumsToTotal\n");
    auto& budget = MemoryBudget::instance();
    const std::size_t total = budget.totalBytes();
    const std::size_t sum = budget.sliceBytes(MemoryBudget::Slice::PageTiles) +
                            budget.sliceBytes(MemoryBudget::Slice::ExcerptTiles) +
                            budget.sliceBytes(MemoryBudget::Slice::UndoStack);
    check(sum <= total, "slices do not exceed the total (" + std::to_string(mb(sum)) +
                            "MB <= " + std::to_string(mb(total)) + "MB)");
    check(budget.sliceBytes(MemoryBudget::Slice::PageTiles) > 0 &&
              budget.sliceBytes(MemoryBudget::Slice::ExcerptTiles) > 0 &&
              budget.sliceBytes(MemoryBudget::Slice::UndoStack) > 0,
          "every slice is non-zero");
}

void testTrimShrinksAndRestoreRecoversExactly() {
    std::printf("testTrimShrinksAndRestoreRecoversExactly\n");
    auto& budget = MemoryBudget::instance();
    budget.restoreFromIdle();

    const std::size_t fullPage = budget.sliceBytes(MemoryBudget::Slice::PageTiles);
    const std::size_t fullExcerpt = budget.sliceBytes(MemoryBudget::Slice::ExcerptTiles);
    const std::size_t fullUndoPerStack = budget.undoBytesPerStack();

    budget.trimToIdle();
    check(budget.isIdle(), "isIdle() after trimToIdle()");
    check(budget.sliceBytes(MemoryBudget::Slice::PageTiles) < fullPage,
          "page slice shrank on trim");
    check(budget.sliceBytes(MemoryBudget::Slice::ExcerptTiles) < fullExcerpt,
          "excerpt slice shrank on trim");
    check(budget.undoBytesPerStack() < fullUndoPerStack, "undo slice shrank on trim");

    budget.restoreFromIdle();
    check(!budget.isIdle(), "isIdle() cleared by restoreFromIdle()");
    check(budget.sliceBytes(MemoryBudget::Slice::PageTiles) == fullPage,
          "page slice restored exactly");
    check(budget.sliceBytes(MemoryBudget::Slice::ExcerptTiles) == fullExcerpt,
          "excerpt slice restored exactly");
    check(budget.undoBytesPerStack() == fullUndoPerStack, "undo slice restored exactly");
}

void testTrimRestoreIsIdempotent() {
    std::printf("testTrimRestoreIsIdempotent\n");
    auto& budget = MemoryBudget::instance();
    budget.restoreFromIdle();
    const std::size_t fullPage = budget.sliceBytes(MemoryBudget::Slice::PageTiles);

    // The idle timer re-fires every window while the app stays idle. Repeated trims must
    // not ratchet slices down toward zero, and repeated restores must not inflate them.
    for (int i = 0; i < 10; ++i) {
        budget.trimToIdle();
    }
    const std::size_t afterManyTrims = budget.sliceBytes(MemoryBudget::Slice::PageTiles);
    for (int i = 0; i < 10; ++i) {
        budget.restoreFromIdle();
    }
    check(afterManyTrims > 0, "repeated trims do not zero the page slice");
    check(budget.sliceBytes(MemoryBudget::Slice::PageTiles) == fullPage,
          "repeated restores converge back to the full page slice");
}

void testUndoSplitAcrossTwoStacks() {
    std::printf("testUndoSplitAcrossTwoStacks\n");
    auto& budget = MemoryBudget::instance();
    budget.restoreFromIdle();
    // There are two independent UndoStacks (document pane + workspace canvas). If
    // undoBytesPerStack() ignored the /2, the pair would claim twice the slice the
    // arbiter authorized and the 64 MB pool would be a 88 MB pool in practice.
    const std::size_t perStack = budget.undoBytesPerStack();
    const std::size_t slice = budget.sliceBytes(MemoryBudget::Slice::UndoStack);
    check(perStack * 2 <= slice, "two undo stacks fit inside the undo slice (" +
                                     std::to_string(mb(perStack * 2)) +
                                     "MB <= " + std::to_string(mb(slice)) + "MB)");
}

} // namespace

int main() {
    testDefaultPartitionSumsToTotal();
    testTrimShrinksAndRestoreRecoversExactly();
    testTrimRestoreIsIdempotent();
    testUndoSplitAcrossTwoStacks();

    if (g_failures == 0) {
        std::printf("MemoryBudgetTest: all checks passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("MemoryBudgetTest: %d check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
