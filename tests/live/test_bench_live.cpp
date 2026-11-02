// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/bench_kernels.hpp>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

using namespace pmp::linux;

TEST_CASE("a short full suite runs and every result is positive", "[live][bench]") {
    const auto dir = defaultBenchDir() / ("live-test-" + std::to_string(::getpid()));
    BenchParams p;
    p.seconds = 0.5;
    p.memBytes = 32u << 20;
    p.latencyBytes = 16u << 20;
    p.diskBytes = 64u << 20;
    p.randomSeconds = 0.5;
    p.tempDir = dir;
    for (int i = 0; i < kBenchCount; ++i) REQUIRE(Benchmark::preflight(static_cast<BenchId>(i), p).empty());
    std::atomic<bool> cancel{false};
    Benchmark b(p);
    for (int i = 0; i < kBenchCount; ++i) {
        const auto r = b.run(static_cast<BenchId>(i), cancel);
        INFO(benchName(static_cast<BenchId>(i)) << ": " << r.note);
        REQUIRE(r.raw > 0);
        REQUIRE(r.score > 0);
        REQUIRE_FALSE(r.cancelled);
        REQUIRE(r.note.find("mismatch") == std::string::npos);
    }
    b.cleanup();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
