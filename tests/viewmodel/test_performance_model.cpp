// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/performance_model.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::testing;

namespace {

std::string valueOf(const std::vector<StatItem>& items, const std::string& caption) {
    for (const auto& i : items)
        if (i.caption == caption) return i.value;
    FAIL("no stat named " << caption);
    return {};
}

bool overlap(const GridCell& a, const GridCell& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

}  // namespace

TEST_CASE("master entries match the spec strings", "[performance]") {
    auto f = populatedFrame(8, 5);
    f.disks.devices.resize(2);
    NetInterface eth;
    eth.name = "eth0";
    f.net.interfaces = {eth};
    const auto entries = masterEntries(f);
    REQUIRE(entries.size() == 4);
    REQUIRE(entries[0].which == Subsystem::Cpu);
    REQUIRE(entries[0].title == "CPU");
    REQUIRE(entries[0].line1 == "15.8%");
    REQUIRE(entries[0].line2 == "8 logical processors");
    REQUIRE(entries[1].line1 == "66.29 GB / 128 GB");
    REQUIRE(entries[1].line2 == "51.8%");
    REQUIRE(entries[2].line1 == "2 physical disks combined");
    REQUIRE(entries[3].line1 == "1 network interfaces combined");
    REQUIRE(entries[3].line2 == "S: 8 KB/s  R: 14 KB/s");
}

TEST_CASE("first frame shows dashes, never zeros", "[performance]") {
    const Frame f;
    for (const auto& e : masterEntries(f)) {
        REQUIRE(e.line1.find("Zero") == std::string::npos);
        REQUIRE(e.line2.find("0.0%") == std::string::npos);
    }
    for (const auto& list : {cpuStats(f), memoryStats(f), diskStats(f), networkStats(f)}) {
        for (const auto& s : list) {
            REQUIRE(s.value.find("Zero") == std::string::npos);
            REQUIRE(s.value.find("0.0%") == std::string::npos);
        }
    }
    REQUIRE(valueOf(memoryStats(f), "In use") == "—");
}

TEST_CASE("unavailable fields read Unavailable", "[performance]") {
    auto f = populatedFrame();
    f.memory.compressed = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
    f.disks.combined.totalRead = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
    f.disks.combined.capacity = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
    f.memory.pressureSomeAvg10 = Metric::notMeasured(Availability::Restricted, Unit::Percent);
    REQUIRE(valueOf(memoryStats(f), "Compressed") == "Unavailable");
    REQUIRE(valueOf(memoryStats(f), "Pressure load") == "Restricted");
    REQUIRE(valueOf(diskStats(f), "Total read") == "Unavailable");
    REQUIRE(valueOf(diskStats(f), "Capacity") == "Unavailable");
    REQUIRE(valueOf(diskStats(f), "Type") == "Unavailable");
}

TEST_CASE("swap used measured zero reads Zero", "[performance]") {
    REQUIRE(valueOf(memoryStats(populatedFrame()), "Swap used") == "Zero KB");
}

TEST_CASE("cpu stats include uptime in D:HH:MM:SS", "[performance]") {
    const auto s = cpuStats(populatedFrame(8, 15));
    REQUIRE(valueOf(s, "Up time") == "5:14:51:19");
    REQUIRE(valueOf(s, "Processes") == "15");
    REQUIRE(valueOf(s, "Logical processors") == "8");
    REQUIRE(valueOf(s, "Utilization") == "15.8%");
}

TEST_CASE("network stats label the combined view", "[performance]") {
    const auto s = networkStats(populatedFrame());
    REQUIRE(valueOf(s, "Interface name") == "All interfaces");
    REQUIRE(valueOf(s, "Connection type") == "Combined");
    REQUIRE(valueOf(s, "Receive") == "14 KB/s");
}

TEST_CASE("64 logical processors lay out without overlap", "[performance]") {
    const auto cells = gridLayout(64, 800, 110, 80, 6);
    REQUIRE(cells.size() == 64);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        REQUIRE(cells[i].x >= 0);
        REQUIRE(cells[i].x + cells[i].w <= 800);
        for (std::size_t j = i + 1; j < cells.size(); ++j) REQUIRE_FALSE(overlap(cells[i], cells[j]));
    }
}

TEST_CASE("a tiny window gets one column and still no overlap", "[performance]") {
    const auto cells = gridLayout(64, 120, 110, 80, 6);
    REQUIRE(cells.size() == 64);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        REQUIRE(cells[i].x + cells[i].w <= 120);
        REQUIRE(cells[i].w > 0);
        for (std::size_t j = i + 1; j < cells.size(); ++j) REQUIRE_FALSE(overlap(cells[i], cells[j]));
    }
    REQUIRE(gridLayout(0, 800, 110, 80, 6).empty());
}

TEST_CASE("master entries list present hardware", "[performance]") {
    REQUIRE(masterEntries(populatedFrame()).size() == 4);
    auto f = populatedFrame();
    withHardware(f);
    const auto e = masterEntries(f);
    REQUIRE(e.size() == 7);
    REQUIRE(e[4].which == Subsystem::Gpu);
    REQUIRE(e[4].title == "GPU 0");
    REQUIRE(e[4].line1 == "Intel Iris Xe Graphics");
    REQUIRE(e[4].line2 == "18.0%");
    REQUIRE(e[5].which == Subsystem::Energy);
    REQUIRE(e[5].line1 == "Thermals nominal · Battery 89%");
    REQUIRE(e[5].line2 == "10 W");
    REQUIRE(e[6].which == Subsystem::Thermals);
    REQUIRE(e[6].line1 == "Nominal thermal pressure");
    REQUIRE(e[6].line2 == "49.0 °C");
}

TEST_CASE("an npu gets its own entry after the gpus", "[performance]") {
    auto f = populatedFrame();
    withHardware(f);
    NpuSnapshot n;
    n.index = 0;
    n.name = "Intel NPU (intel_vpu)";
    n.activePct = Metric::measured(50, Unit::Percent);
    f.npus = {n};
    const auto e = masterEntries(f);
    REQUIRE(e[5].which == Subsystem::Npu);
    REQUIRE(e[5].line2 == "50.0% active time");
}

TEST_CASE("gpu stats", "[performance]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto s = gpuStats(f.gpus[0]);
    REQUIRE(valueOf(s, "Utilization") == "18.0%");
    REQUIRE(valueOf(s, "GPU memory (resident)") == "385.2 MB");
    REQUIRE(valueOf(s, "Frequency") == "1.30 GHz");
    REQUIRE(valueOf(s, "Temperature") == "Unavailable");
    REQUIRE(valueOf(s, "Driver") == "i915");
    REQUIRE(valueOf(s, "PCI slot") == "0000:00:02.0");
    REQUIRE(valueOf(s, "Render engine") == "18.0%");
}

TEST_CASE("a gpu with dedicated memory shows used over total", "[performance]") {
    auto f = populatedFrame();
    withHardware(f);
    f.gpus[0].memTotal = Metric::measured(8.0 * GiB, Unit::Bytes);
    f.gpus[0].memUsed = Metric::measured(2.0 * GiB, Unit::Bytes);
    REQUIRE(valueOf(gpuStats(f.gpus[0]), "Dedicated memory") == "2 GB / 8 GB");
}
