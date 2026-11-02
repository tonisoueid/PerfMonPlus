// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/system_info.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

using namespace pmp;
using Catch::Approx;
using pmp::linux::collectSystemInfo;

namespace {

void layout(FakeRoot& fr) {
    fr.write("etc/os-release", "NAME=\"Ubuntu\"\nPRETTY_NAME=\"Ubuntu 26.04.1 LTS\"\nVERSION_ID=\"26.04\"\n");
    fr.write("proc/sys/kernel/osrelease", "7.0.0-38-generic\n");
    fr.write("proc/sys/kernel/hostname", "framework\n");
    fr.write("proc/stat", "cpu  1 0 1 1 0 0 0 0 0 0\nbtime 1792833000\n");
    fr.write("proc/uptime", "10000.50 5000.00\n");
    fr.write("proc/meminfo", "MemTotal:       16072000 kB\nSwapTotal:       4194300 kB\nSwapFree:        4194300 kB\n");
    fr.write("sys/class/dmi/id/sys_vendor", "LG Electronics\n");
    fr.write("sys/class/dmi/id/product_name", "17Z90P-G.AA75F\n");
    fr.write("sys/class/dmi/id/bios_version", "T2ZF0360 X64\n");
    fr.write("sys/class/dmi/id/product_serial", "ABC123\n");
    fr.write("sys/class/net/wlp0s20f3/address", "8c:b8:7e:d9:0e:c2\n");
    fr.makeDir("sys/class/net/wlp0s20f3/device");
    fr.makeDir("sys/class/net/wlp0s20f3/speed");  // reading it fails (like EINVAL on an unconnected Wi-Fi link)
    fr.write("sys/class/net/eth0/address", "00:11:22:33:44:55\n");
    fr.makeDir("sys/class/net/eth0/device");
    fr.write("sys/class/net/eth0/speed", "1000\n");
    fr.write("sys/class/net/lo/address", "00:00:00:00:00:00\n");
}

}  // namespace

TEST_CASE("operating system and machine identity", "[sysinfo]") {
    FakeRoot fr;
    layout(fr);
    const auto s = collectSystemInfo(fr.root(), fr.root().proc.parent_path() / "etc");
    REQUIRE(s.osName == "Ubuntu 26.04.1 LTS");
    REQUIRE(s.kernel == "7.0.0-38-generic");
    REQUIRE(s.hostname == "framework");
    REQUIRE(s.vendor == "LG Electronics");
    REQUIRE(s.model == "17Z90P-G.AA75F");
    REQUIRE(s.biosVersion == "T2ZF0360 X64");
    REQUIRE(s.serial == "ABC123");
    REQUIRE(*s.bootTime.value == 1792833000.0);
    REQUIRE(*s.uptime.value == Approx(10000.5));
    REQUIRE(*s.memTotal.value == 16072000.0 * 1024);
    REQUIRE(*s.swapTotal.value == 4194300.0 * 1024);
}

TEST_CASE("an unreadable serial is empty, not an error", "[sysinfo]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    layout(fr);
    fr.makeUnreadable("sys/class/dmi/id/product_serial");
    const auto s = collectSystemInfo(fr.root(), fr.root().proc.parent_path() / "etc");
    REQUIRE(s.serial.empty());
    REQUIRE(s.vendor == "LG Electronics");
}

TEST_CASE("link speed that cannot be read is unavailable", "[sysinfo]") {
    FakeRoot fr;
    layout(fr);
    const auto s = collectSystemInfo(fr.root(), fr.root().proc.parent_path() / "etc");
    const auto* wifi = [&]() -> const linux::NetAdapter* {
        for (const auto& a : s.adapters)
            if (a.name == "wlp0s20f3") return &a;
        return nullptr;
    }();
    REQUIRE(wifi);
    REQUIRE(wifi->mac == "8c:b8:7e:d9:0e:c2");
    REQUIRE_FALSE(wifi->isVirtual);
    REQUIRE_FALSE(wifi->speedBps.isMeasured());
    for (const auto& a : s.adapters) {
        if (a.name == "eth0") REQUIRE(*a.speedBps.value == 1000.0 * 125000.0);
        if (a.name == "lo") REQUIRE(a.isVirtual);
    }
}

TEST_CASE("missing files are tolerated", "[sysinfo]") {
    FakeRoot fr;
    const auto s = collectSystemInfo(fr.root(), fr.root().proc.parent_path() / "etc");
    REQUIRE(s.osName.empty());
    REQUIRE(s.hostname.empty());
    REQUIRE(s.adapters.empty());
    REQUIRE_FALSE(s.secureBoot.has_value());
    REQUIRE_FALSE(s.memTotal.isMeasured());
}
