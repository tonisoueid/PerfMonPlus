// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/summary_model.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::testing;
using Catch::Approx;

TEST_CASE("first frame shows no zeros", "[summary]") {
    const Frame f;
    const auto vfd = vfdStack(f);
    REQUIRE_FALSE(vfd[0].measured);
    REQUIRE(vfd[0].value == "—");
    REQUIRE(diskTile(f).value == "—");
    REQUIRE(networkTile(f).value == "—");
    REQUIRE(memoryHeader(f) == "Unavailable");
    REQUIRE(topProcesses(f, 12).empty());
}

TEST_CASE("vfd stack values", "[summary]") {
    const auto vfd = vfdStack(populatedFrame());
    REQUIRE(vfd[0].label == "CPU");
    REQUIRE(vfd[0].value == "15.8%");
    REQUIRE(vfd[0].fraction == Approx(0.158));
    REQUIRE(vfd[0].measured);
    for (int i = 1; i < 4; ++i) {
        REQUIRE_FALSE(vfd[static_cast<std::size_t>(i)].measured);
        REQUIRE(vfd[static_cast<std::size_t>(i)].value == "Unavailable");
    }
    REQUIRE(vfd[1].label == "Clock");
    REQUIRE(vfd[2].label == "Temp");
    REQUIRE(vfd[3].label == "GPU");
}

TEST_CASE("disk tile text", "[summary]") {
    const auto t = diskTile(populatedFrame());
    REQUIRE(t.title == "Disks");
    REQUIRE(t.value == "0.0%");
    REQUIRE(t.subtitle == "R: Zero KB/s  W: 32 KB/s");
    REQUIRE(t.available);
}

TEST_CASE("network tile text", "[summary]") {
    const auto t = networkTile(populatedFrame());
    REQUIRE(t.title == "Network");
    REQUIRE(t.value == "22 KB/s");
    REQUIRE(t.subtitle == "R: 14 KB/s  S: 8 KB/s");
}

TEST_CASE("memory header", "[summary]") {
    REQUIRE(memoryHeader(populatedFrame()) == "66.29 GB / 128 GB");
}

TEST_CASE("top processes ordering and limit", "[summary]") {
    auto f = populatedFrame(8, 15);
    ProcessRecord dead;
    dead.key = {99, 5};
    dead.name = "dead";
    dead.cpu = pct(500);
    dead.exitedAt = MonoTime{};
    f.processes.processes.push_back(dead);
    const auto rows = topProcesses(f, 12);
    REQUIRE(rows.size() == 12);
    REQUIRE(rows[0].pid == 15);
    REQUIRE(rows[0].cpu == "15.0%");
    REQUIRE(rows[0].gpu == "—");
    for (const auto& r : rows) REQUIRE(r.pid != 99);
    for (std::size_t i = 1; i < rows.size(); ++i) REQUIRE(rows[i - 1].pid > rows[i].pid);
}

TEST_CASE("pending cpu sorts last and shows a dash", "[summary]") {
    Frame f;
    ProcessRecord a, b;
    a.key = {1, 1};
    a.name = "a";  // cpu stays Pending
    b.key = {2, 2};
    b.name = "b";
    b.cpu = pct(1);
    f.processes.processes = {a, b};
    const auto rows = topProcesses(f, 12);
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].pid == 2);
    REQUIRE(rows[1].cpu == "—");
}

TEST_CASE("per-process gpu is shown when measured", "[summary]") {
    auto f = populatedFrame(8, 3);
    f.processes.processes[2].gpu = Metric::measured(12.5, Unit::Percent);
    const auto rows = topProcesses(f, 12);
    REQUIRE(rows[0].pid == 3);
    REQUIRE(rows[0].gpu == "12.5%");
    REQUIRE(rows[1].gpu == "—");  // Pending until the scan has covered it
}
