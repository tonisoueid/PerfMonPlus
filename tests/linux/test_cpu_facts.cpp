// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/cpu_facts.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using Catch::Approx;
using pmp::linux::collectCpuFacts;

namespace {

void layout(FakeRoot& fr, bool caches = true) {
    fr.write("proc/cpuinfo",
             "processor\t: 0\nmodel name\t: 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\nflags\t\t: fpu vmx sse\n\n"
             "processor\t: 1\nmodel name\t: 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\nflags\t\t: fpu vmx sse\n");
    for (int i = 0; i < 2; ++i) {
        const auto base = "sys/devices/system/cpu/cpu" + std::to_string(i) + "/";
        fr.write(base + "topology/physical_package_id", "0\n");
        fr.write(base + "topology/core_id", "0\n");
    }
    const std::string c0 = "sys/devices/system/cpu/cpu0/";
    fr.write(c0 + "cpufreq/base_frequency", "2800000\n");
    fr.write(c0 + "cpufreq/scaling_driver", "intel_pstate\n");
    fr.write(c0 + "cpufreq/scaling_governor", "powersave\n");
    fr.write(c0 + "cpufreq/energy_performance_preference", "balance_power\n");
    fr.write(c0 + "cpufreq/cpuinfo_max_freq", "4700000\n");
    if (!caches) return;
    const char* idx[][3] = {{"1", "Data", "48K"}, {"1", "Instruction", "32K"}, {"2", "Unified", "1280K"}, {"3", "Unified", "12288K"}};
    for (int i = 0; i < 4; ++i) {
        const auto d = c0 + "cache/index" + std::to_string(i) + "/";
        fr.write(d + "level", std::string(idx[i][0]) + "\n");
        fr.write(d + "type", std::string(idx[i][1]) + "\n");
        fr.write(d + "size", std::string(idx[i][2]) + "\n");
    }
}

}  // namespace

TEST_CASE("cpu facts from sysfs and cpuinfo", "[cpufacts]") {
    FakeRoot fr;
    layout(fr);
    const auto f = collectCpuFacts(fr.root());
    REQUIRE(f.model == "11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz");
    REQUIRE(f.sockets == 1);
    REQUIRE(f.physicalCores == 1);
    REQUIRE(f.logical == 2);
    REQUIRE(*f.l1d.value == 48.0 * 1024);
    REQUIRE(*f.l1i.value == 32.0 * 1024);
    REQUIRE(*f.l2.value == 1280.0 * 1024);
    REQUIRE(*f.l3.value == 12288.0 * 1024);
    REQUIRE(*f.baseHz.value == Approx(2.8e9));
    REQUIRE(*f.maxHz.value == Approx(4.7e9));
    REQUIRE(f.driver == "intel_pstate");
    REQUIRE(f.governor == "powersave");
    REQUIRE(f.epp == "balance_power");
    REQUIRE(f.virtualization == true);
    REQUIRE(f.virtualMachine == false);
}

TEST_CASE("missing cache information is Unsupported", "[cpufacts]") {
    FakeRoot fr;
    layout(fr, false);
    const auto f = collectCpuFacts(fr.root());
    REQUIRE(f.l3.availability == Availability::Unsupported);
    REQUIRE(f.logical == 2);
}

TEST_CASE("an empty system gives empty facts without throwing", "[cpufacts]") {
    FakeRoot fr;
    const auto f = collectCpuFacts(fr.root());
    REQUIRE(f.model.empty());
    REQUIRE(f.logical == 0);
    REQUIRE_FALSE(f.virtualization.has_value());
    REQUIRE(f.baseHz.availability == Availability::Unsupported);
}
