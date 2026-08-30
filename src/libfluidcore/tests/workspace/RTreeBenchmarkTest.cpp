// RTreeBenchmarkTest.cpp — Headless performance benchmark for RTreeIndex (ROADMAP §5: <= 1ms p99 on
// 10^5 items)

#include "workspace/RTreeIndex.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

using namespace FluidCore;

struct Lcg {
    std::uint64_t state = 0x854329014872ULL;
    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 11;
    }
    double unit() { return static_cast<double>(next() % 1000000) / 1000000.0; }
};

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    int failures = 0;
    std::cout << "Running RTreeBenchmarkTest: 100,000 items spatial query benchmark...\n";

    RTreeIndex index;
    Lcg rng;

    constexpr std::size_t kNumItems = 100000;
    constexpr std::size_t kNumQueries = 2000;
    constexpr double kCanvasExtent = 20000.0; // 20,000 x 20,000 pt canvas

    auto startInsert = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < kNumItems; ++i) {
        const double x = rng.unit() * (kCanvasExtent - 300.0);
        const double y = rng.unit() * (kCanvasExtent - 300.0);
        const double w = 50.0 + rng.unit() * 200.0;
        const double h = 40.0 + rng.unit() * 150.0;
        index.insert({x, y, w, h});
    }
    auto endInsert = std::chrono::high_resolution_clock::now();
    double insertTimeMs =
        std::chrono::duration<double, std::milli>(endInsert - startInsert).count();
    std::cout << "  Inserted " << kNumItems << " items in " << std::fixed << std::setprecision(2)
              << insertTimeMs << " ms (" << (insertTimeMs / kNumItems * 1000.0) << " us/insert)\n";

    failures += check(index.size() == kNumItems, "index has 100k items");

    // Perform kNumQueries randomized viewport queries
    std::vector<double> latenciesUs;
    latenciesUs.reserve(kNumQueries);
    std::size_t totalHits = 0;

    for (std::size_t q = 0; q < kNumQueries; ++q) {
        // Typical screen viewports (800x600 to 2400x1600 in world units)
        const double vw = 800.0 + rng.unit() * 1600.0;
        const double vh = 600.0 + rng.unit() * 1000.0;
        const double vx = rng.unit() * (kCanvasExtent - vw);
        const double vy = rng.unit() * (kCanvasExtent - vh);
        const Rectangle viewport{vx, vy, vw, vh};

        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<RTreeIndex::Handle> results = index.query(viewport);
        auto t1 = std::chrono::high_resolution_clock::now();

        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latenciesUs.push_back(us);
        totalHits += results.size();
    }

    std::sort(latenciesUs.begin(), latenciesUs.end());
    double p50 = latenciesUs[static_cast<std::size_t>(kNumQueries * 0.50)];
    double p90 = latenciesUs[static_cast<std::size_t>(kNumQueries * 0.90)];
    double p99 = latenciesUs[static_cast<std::size_t>(kNumQueries * 0.99)];
    double maxLat = latenciesUs.back();
    double avgHits = static_cast<double>(totalHits) / static_cast<double>(kNumQueries);

    std::cout << "  Query benchmark results across " << kNumQueries << " viewports:\n"
              << "    Avg hits per viewport: " << std::setprecision(1) << avgHits << "\n"
              << "    p50 latency: " << std::setprecision(2) << (p50 / 1000.0) << " ms (" << p50
              << " us)\n"
              << "    p90 latency: " << std::setprecision(2) << (p90 / 1000.0) << " ms (" << p90
              << " us)\n"
              << "    p99 latency: " << std::setprecision(2) << (p99 / 1000.0) << " ms (" << p99
              << " us)\n"
              << "    max latency: " << std::setprecision(2) << (maxLat / 1000.0) << " ms ("
              << maxLat << " us)\n";

    // ROADMAP §5 Budget: p99 <= 1.0 ms (1000 us)
    failures += check(p99 <= 1000.0, "p99 spatial query latency <= 1.0 ms budget");

    if (failures == 0) {
        std::cout << "RTreeBenchmarkTest: PASSED (ROADMAP §5 budget satisfied)\n";
        return 0;
    }
    std::cerr << "RTreeBenchmarkTest: FAILED (" << failures << " error(s))\n";
    return 1;
}
