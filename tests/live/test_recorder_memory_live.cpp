// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/player.hpp>
#include <perfmonplus/trace/recorder.hpp>

#include "support/full_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

using namespace pmp;
using namespace pmp::trace;

namespace {

double rssMB() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        double kb = 0;
        if (std::sscanf(line.c_str(), "VmRSS: %lf kB", &kb) == 1) return kb / 1024.0;
    }
    return -1;
}

}  // namespace

TEST_CASE("thirty minutes of recording stays well under the memory budget", "[live][recorder]") {
    HostMeta host;
    host.os = "Linux test";
    RecorderOptions o;
    o.maxLength = std::chrono::minutes(60);
    Recorder rec(host, std::chrono::milliseconds(1000), o);
    rec.start();
    auto f = testing::fullFrame(300);  // a busy machine: 300 processes
    const double before = rssMB();
    double worstMs = 0;
    for (int i = 0; i < 1800; ++i) {  // 30 minutes at 1 Hz
        f.mono = MonoTime{} + std::chrono::seconds(i);
        f.wall = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(1'700'000'000'000 + i * 1000LL);
        for (auto& p : f.processes.processes) p.cpu = Metric::measured(((i * 7 + p.key.pid) % 100) / 4.0, Unit::Percent);
        const auto t = std::chrono::steady_clock::now();
        rec.append(f, false);
        worstMs = std::max(worstMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
    }
    rec.stop();
    const double after = rssMB();
    std::printf("30 min, 300 processes: %zu compressed bytes, RSS %+.1f MB, worst append %.2f ms\n", rec.compressedBytes(), after - before, worstMs);
    REQUIRE(rec.sampleCount() == 1800);
    REQUIRE(rec.compressedBytes() < 30'000'000);       // 1 MB/min worst case (FLR-09)
    REQUIRE(after - before < 80.0);                    // far inside NFR-03's 150 MB for the whole app
#ifdef PMP_OPTIMIZED
    REQUIRE(worstMs < 8.0);                             // a chunk seal on the UI thread never costs a visible stutter
#endif
}

TEST_CASE("a two-hour trace opens fast enough to feel instant", "[live][recorder]") {
    HostMeta host;
    host.os = "Linux test";
    RecorderOptions o;
    o.maxLength = std::chrono::minutes(180);
    Recorder rec(host, std::chrono::milliseconds(1000), o);
    rec.start();
    auto f = testing::fullFrame(300);
    for (int i = 0; i < 7200; ++i) {  // 2 hours at 1 Hz
        f.mono = MonoTime{} + std::chrono::seconds(i);
        f.wall = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(1'700'000'000'000 + i * 1000LL);
        for (auto& p : f.processes.processes) p.cpu = Metric::measured(((i * 7 + p.key.pid) % 100) / 4.0, Unit::Percent);
        rec.append(f, false);
    }
    rec.stop();
    const auto path = std::filesystem::temp_directory_path() / ("pmp-open-" + std::to_string(::getpid()) + ".pmptrace");
    std::string err;
    REQUIRE(rec.save(path, &err));
    const auto bytes = std::filesystem::file_size(path);
    Player pl;
    const auto t = std::chrono::steady_clock::now();
    const auto r = pl.open(path);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    std::filesystem::remove(path);
    std::printf("2 h, 300 processes: %.1f MB file, open %.0f ms\n", bytes / 1048576.0, ms);
    REQUIRE(r.ok());
    REQUIRE(pl.sampleCount() == 7200);
#ifdef PMP_OPTIMIZED
    REQUIRE(ms < 1500.0);
#endif
}
