// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/disk_scan.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace pmp::linux;

TEST_CASE("scanning / skips /proc and /sys and finishes", "[live][disk-scan]") {
    std::atomic<bool> cancel{false};
    ScanOptions o;
    o.root = "/";
    o.lowPriority = false;
    std::atomic<bool> stop{false};
    o.beforeStat = [&](const std::filesystem::path&) {
        static std::uint64_t n = 0;
        if (++n > 400000) stop = true;  // keep the test short on very large systems
    };
    const auto r = DiskScanner().scan(o, stop);
    REQUIRE(r.skippedMounts >= 2);  // at least /proc and /sys
    REQUIRE(r.root.bytes > 0);
    for (const auto& c : r.root.children) {
        REQUIRE(c.rawName != "proc");  // folded or kept, but its contents were never read: it is at most a bare directory
        if (c.rawName == "proc") REQUIRE(c.files == 0);
    }
}

TEST_CASE("sum of children never exceeds the root total", "[live][disk-scan]") {
    std::atomic<bool> cancel{false};
    ScanOptions o;
    o.root = "/usr/share";
    o.lowPriority = false;
    const auto r = DiskScanner().scan(o, cancel);
    std::uint64_t sum = 0;
    for (const auto& c : r.root.children) sum += c.bytes;
    REQUIRE(sum <= r.root.bytes);
    REQUIRE(r.root.bytes - sum < 1'000'000);  // only the directory's own blocks differ
    REQUIRE_FALSE(r.largest.empty());
}

TEST_CASE("real mounts include / and flag proc as pseudo", "[live][disk-scan]") {
    const auto mounts = readMounts(ProcRoot::live());
    bool root = false, proc = false;
    for (const auto& m : mounts) {
        if (m.mountPoint == "/") {
            root = true;
            REQUIRE_FALSE(m.pseudo);
            REQUIRE(m.totalBytes > 0);
        }
        if (m.mountPoint == "/proc") {
            proc = true;
            REQUIRE(m.pseudo);
        }
    }
    REQUIRE(root);
    REQUIRE(proc);
}
