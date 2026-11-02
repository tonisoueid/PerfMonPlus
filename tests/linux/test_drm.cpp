// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/drm.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pmp::linux;

TEST_CASE("drm memory values parse with units", "[drm]") {
    REQUIRE(parseDrmMemory("394480 KiB") == 394480.0 * 1024);
    REQUIRE(parseDrmMemory("64 MiB") == 64.0 * 1024 * 1024);
    REQUIRE(parseDrmMemory("2 GiB") == 2.0 * 1024 * 1024 * 1024);
    REQUIRE(parseDrmMemory("512") == 512.0);
    REQUIRE_FALSE(parseDrmMemory("x").has_value());
}

TEST_CASE("fdinfo lines parse into strings", "[drm]") {
    const auto m = parseColonStrings("drm-pdev:\t0000:00:02.0\ndrm-engine-render:\t1000 ns\nbad line\n");
    REQUIRE(m.at("drm-pdev") == "0000:00:02.0");
    REQUIRE(m.at("drm-engine-render") == "1000 ns");
    REQUIRE(m.size() == 2);
}

TEST_CASE("gpu names come from pci.ids with a fallback", "[drm]") {
    FakeRoot fr;
    fr.write("pci.ids", "8086  Intel Corporation\n\t9a49  TigerLake-LP GT2 [Iris Xe Graphics]\n\t9a60  Other\n");
    fr.write("sys/devices/pci0000:00/0000:00:02.0/vendor", "0x8086\n");
    fr.write("sys/devices/pci0000:00/0000:00:02.0/device", "0x9a49\n");
    fr.symlink("../../../devices/pci0000:00/0000:00:02.0", "sys/class/drm/card1/device");
    const auto ids = fr.root().proc.parent_path() / "pci.ids";
    REQUIRE(gpuName(fr.root(), "card1", ids) == "TigerLake-LP GT2 [Iris Xe Graphics]");
    REQUIRE(gpuName(fr.root(), "card1", ids.parent_path() / "missing.ids") == "Intel GPU");
}

namespace {
void fakeProcesses(FakeRoot& fr, int count) {
    for (int pid = 1000; pid < 1000 + count; ++pid) {
        fr.symlink("/dev/null", "proc/" + std::to_string(pid) + "/fd/3");
    }
}
}  // namespace

TEST_CASE("discovery looks at a bounded share of processes per scan", "[drm]") {
    FakeRoot fr;
    fakeProcesses(fr, 50);
    std::vector<std::int32_t> live;
    for (int pid = 1000; pid < 1050; ++pid) live.push_back(pid);
    DrmClientScanner scanner(fr.root(), 10);
    scanner.scan(live, {});
    REQUIRE(scanner.lastScannedPidCount() == 5);
    for (int i = 0; i < 9; ++i) scanner.scan(live, {});
    REQUIRE(scanner.lastScannedPidCount() == 5);
    scanner.scan(live, {1000, 1001});  // new processes are always looked at
    REQUIRE(scanner.lastScannedPidCount() >= 5);
}
