// Headless unit tests for the RTreeIndex slice (mirrors workspace/RTreeIndex.cpp).

#include "workspace/RTreeIndex.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

using namespace FluidCore;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

bool sameHandles(std::vector<RTreeIndex::Handle> a, std::vector<RTreeIndex::Handle> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

struct Lcg {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint64_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state >> 11;
    }
    double unit() { return static_cast<double>(next() % 1000000) / 1000000.0; }
};

std::vector<RTreeIndex::Handle>
oracleQuery(const std::vector<std::pair<RTreeIndex::Handle, Rectangle>>& live,
            const Rectangle& region) {
    std::vector<RTreeIndex::Handle> out;
    for (const auto& [handle, rect] : live) {
        if (rect.x <= region.x + region.w && region.x <= rect.x + rect.w &&
            rect.y <= region.y + region.h && region.y <= rect.y + rect.h) {
            out.push_back(handle);
        }
    }
    return out;
}

int testHitAndMiss() {
    int failures = 0;
    RTreeIndex index;
    failures += check(index.empty(), "fresh index is empty");

    const RTreeIndex::Handle a = index.insert({10.0, 10.0, 20.0, 20.0});
    const RTreeIndex::Handle b = index.insert({100.0, 100.0, 30.0, 30.0});
    failures += check(index.size() == 2, "size tracks inserts");
    failures += check(a != RTreeIndex::kInvalidHandle && b != a, "handles are distinct");

    failures += check(sameHandles(index.query({0.0, 0.0, 50.0, 50.0}), {a}),
                      "viewport over A finds only A");
    failures += check(sameHandles(index.query({90.0, 90.0, 100.0, 100.0}), {b}),
                      "viewport over B finds only B");
    failures += check(sameHandles(index.query({500.0, 500.0, 10.0, 10.0}), {}),
                      "far viewport finds nothing");
    failures += check(sameHandles(index.query({0.0, 0.0, 1000.0, 1000.0}), {a, b}),
                      "wide viewport finds both");
    failures += check(sameHandles(index.query({30.0, 30.0, 70.0, 70.0}), {a, b}),
                      "touching edges count as overlap with inclusive bounds");
    return failures;
}

int testRemoveAndUpdate() {
    int failures = 0;
    RTreeIndex index;
    const RTreeIndex::Handle a = index.insert({0.0, 0.0, 10.0, 10.0});
    const RTreeIndex::Handle b = index.insert({50.0, 50.0, 10.0, 10.0});

    failures += check(!index.remove(9999), "unknown handle removal fails cleanly");
    failures += check(index.remove(a), "known handle removes");
    failures += check(index.size() == 1 && !index.empty(), "remove shrinks the index");
    failures += check(sameHandles(index.query({0.0, 0.0, 100.0, 100.0}), {b}),
                      "removed entry leaves the query results");
    failures += check(!index.remove(a), "double remove fails cleanly");

    const RTreeIndex::Handle c = index.insert({0.0, 0.0, 5.0, 5.0});
    failures += check(sameHandles(index.query({0.0, 0.0, 100.0, 100.0}), {b, c}),
                      "reinsert after remove works");

    index.update(b, {200.0, 200.0, 10.0, 10.0});
    failures += check(sameHandles(index.query({50.0, 50.0, 10.0, 10.0}), {}),
                      "updated entry vacated its old region");
    failures += check(sameHandles(index.query({195.0, 195.0, 20.0, 20.0}), {b}),
                      "updated entry appears at its new location");
    index.update(9999, {0.0, 0.0, 1.0, 1.0}); // must be a silent no-op
    failures += check(index.size() == 2, "update of unknown handle changes nothing");
    return failures;
}

int testDeepSplitBoundsPropagation() {
    int failures = 0;
    RTreeIndex index;
    std::vector<std::pair<RTreeIndex::Handle, Rectangle>> live;

    // Insert 60 elements to force multiple recursive node splits and parent tightens
    for (int i = 0; i < 60; ++i) {
        Rectangle r{static_cast<double>(i * 30), static_cast<double>(i * 30), 20.0, 20.0};
        RTreeIndex::Handle h = index.insert(r);
        live.emplace_back(h, r);
    }
    failures += check(index.size() == 60, "deep split inserted 60 items");

    // Small zoomed-in viewport querying a specific child element
    for (int i = 0; i < 60; i += 5) {
        Rectangle probe{static_cast<double>(i * 30 + 5), static_cast<double>(i * 30 + 5), 5.0,
                        5.0};
        auto found = index.query(probe);
        auto expected = oracleQuery(live, probe);
        failures += check(sameHandles(found, expected),
                          "child node viewport query succeeds after tree splits");
    }
    return failures;
}

int testInclusiveAndDegenerateBounds() {
    int failures = 0;
    RTreeIndex index;

    // Contact on exact edge
    RTreeIndex::Handle h1 = index.insert({0.0, 0.0, 10.0, 10.0});
    RTreeIndex::Handle h2 = index.insert({10.0, 0.0, 10.0, 10.0});

    // Probe landing on x=10 boundary line
    auto onBoundary = index.query({10.0, 2.0, 0.0, 0.0});
    failures += check(sameHandles(onBoundary, {h1, h2}),
                      "zero-size probe on shared boundary hits both touching boxes");

    // Degenerate zero-size point stroke/rect
    RTreeIndex::Handle hPoint = index.insert({50.0, 50.0, 0.0, 0.0});
    failures += check(sameHandles(index.query({50.0, 50.0, 0.0, 0.0}), {hPoint}),
                      "zero-size query hits zero-size point entry");
    failures += check(sameHandles(index.query({49.0, 49.0, 2.0, 2.0}), {hPoint}),
                      "area query containing zero-size point hits");
    failures += check(sameHandles(index.query({50.01, 50.01, 1.0, 1.0}), {}),
                      "disjoint query misses zero-size point");

    return failures;
}

int testEmptyRootCollapseAndReinsert() {
    int failures = 0;
    RTreeIndex index;

    std::vector<RTreeIndex::Handle> handles;
    for (int i = 0; i < 20; ++i) {
        handles.push_back(index.insert({static_cast<double>(i * 10), 0.0, 5.0, 5.0}));
    }

    // Update entries
    for (auto h : handles) {
        index.update(h, {100.0, 100.0, 5.0, 5.0});
    }

    // Remove all entries
    for (auto h : handles) {
        failures += check(index.remove(h), "entry removes cleanly");
    }
    failures += check(index.empty(), "index is empty after removing all");
    failures += check(index.query({0.0, 0.0, 200.0, 200.0}).empty(),
                      "query on collapsed empty root returns empty");

    // Reinsert on collapsed empty root
    RTreeIndex::Handle newH = index.insert({10.0, 10.0, 20.0, 20.0});
    failures += check(newH != RTreeIndex::kInvalidHandle, "insert into collapsed root succeeds");
    failures += check(sameHandles(index.query({0.0, 0.0, 50.0, 50.0}), {newH}),
                      "query on reinserted entry finds new handle");

    return failures;
}

int testOracleStress() {
    int failures = 0;
    RTreeIndex index;
    Lcg rng;
    std::vector<std::pair<RTreeIndex::Handle, Rectangle>> live;
    RTreeIndex::Handle nextHandle = 1;

    auto randomRect = [&rng] {
        return Rectangle{rng.unit() * 900.0, rng.unit() * 900.0, 5.0 + rng.unit() * 40.0,
                         5.0 + rng.unit() * 40.0};
    };

    for (int step = 0; step < 3000; ++step) {
        const bool mustInsert = live.empty() || live.size() >= 400;
        if (mustInsert || rng.unit() < 0.55) {
            const Rectangle rect = randomRect();
            const RTreeIndex::Handle h = index.insert(rect);
            failures += check(h == nextHandle, "stress handles stay sequential");
            ++nextHandle;
            live.emplace_back(h, rect);
        } else if (rng.unit() < 0.6) {
            const std::size_t victim = static_cast<std::size_t>(rng.next()) % live.size();
            failures += check(index.remove(live[victim].first), "stress remove succeeds");
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(victim));
        } else {
            const std::size_t mover = static_cast<std::size_t>(rng.next()) % live.size();
            const Rectangle moved = randomRect();
            index.update(live[mover].first, moved);
            live[mover].second = moved;
        }

        if (step % 60 == 0) {
            for (int probe = 0; probe < 4; ++probe) {
                const Rectangle region{static_cast<double>(probe) * 250.0,
                                       static_cast<double>((probe * 7) % 4) * 250.0, 260.0, 260.0};
                failures += check(sameHandles(index.query(region), oracleQuery(live, region)),
                                  "stress query matches brute force");
                if (failures > 0)
                    return failures;
            }
        }
    }

    while (!live.empty()) {
        index.remove(live.back().first);
        live.pop_back();
    }
    failures += check(index.empty(), "draining every entry empties the index");
    failures += check(sameHandles(index.query({0.0, 0.0, 1000.0, 1000.0}), {}),
                      "emptied index queries nothing");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += testHitAndMiss();
    failures += testRemoveAndUpdate();
    failures += testDeepSplitBoundsPropagation();
    failures += testInclusiveAndDegenerateBounds();
    failures += testEmptyRootCollapseAndReinsert();
    failures += testOracleStress();

    if (failures == 0) {
        std::cout << "RTreeIndexTest: all checks passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
