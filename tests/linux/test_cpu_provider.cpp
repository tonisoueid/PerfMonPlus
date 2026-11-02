// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/cpu_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

using namespace pmp;
using Catch::Approx;
using pmp::linux::CpuProvider;

namespace {

constexpr const char* kStat0 =
    "cpu  100 0 100 800 0 0 0 0 0 0\n"
    "cpu0 50 0 50 400 0 0 0 0 0 0\n"
    "cpu1 50 0 50 400 0 0 0 0 0 0\n";
constexpr const char* kStat1 =
    "cpu  400 0 200 1400 0 0 0 0 0 0\n"
    "cpu0 300 0 100 600 0 0 0 0 0 0\n"
    "cpu1 100 0 100 800 0 0 0 0 0 0\n";

void capacities(FakeRoot& fr) {
    fr.write("sys/devices/system/cpu/cpu0/cpu_capacity", "1024\n");
    fr.write("sys/devices/system/cpu/cpu1/cpu_capacity", "512\n");
}

Frame sampleAt(CpuProvider& p, int gen) {
    Frame f;
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), MonoTime{}});
    return f;
}

}  // namespace

TEST_CASE("first cpu sample is Pending", "[cpu]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "12345.67 54321.00\n");
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.total.availability == Availability::Pending);
    REQUIRE(f.cpu.logical.size() == 2);
    REQUIRE(f.cpu.logical[0].util.availability == Availability::Pending);
    REQUIRE(f.cpu.uptime.value == Approx(12345.67));
}

TEST_CASE("cpu utilisation from deltas", "[cpu]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    CpuProvider p(fr.root());
    sampleAt(p, 1);
    fr.write("proc/stat", kStat1);
    auto f = sampleAt(p, 2);
    REQUIRE(*f.cpu.total.value == Approx(40.0));
    REQUIRE(*f.cpu.kernel.value == Approx(10.0));
    REQUIRE(*f.cpu.logical[0].util.value == Approx(60.0));
    REQUIRE(*f.cpu.logical[1].util.value == Approx(20.0));
    REQUIRE(*f.cpu.logical[1].kernel.value == Approx(10.0));
}

TEST_CASE("core classes come from capacity or hybrid sysfs lists", "[cpu]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    capacities(fr);
    {
        CpuProvider p(fr.root());
        auto f = sampleAt(p, 1);
        REQUIRE(f.cpu.logical[0].coreClass == CoreClass::Performance);
        REQUIRE(f.cpu.logical[1].coreClass == CoreClass::Efficiency);
    }
    fr.remove("sys/devices/system/cpu");
    fr.write("sys/devices/cpu_core/cpus", "1\n");
    fr.write("sys/devices/cpu_atom/cpus", "0\n");
    {
        CpuProvider p(fr.root());
        auto f = sampleAt(p, 1);
        REQUIRE(f.cpu.logical[0].coreClass == CoreClass::Efficiency);
        REQUIRE(f.cpu.logical[1].coreClass == CoreClass::Performance);
    }
    fr.remove("sys/devices");
    {
        CpuProvider p(fr.root());
        auto f = sampleAt(p, 1);
        REQUIRE(f.cpu.logical[0].coreClass == CoreClass::Unknown);
    }
}

TEST_CASE("offline cpu and negative deltas are Pending", "[cpu]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat1);
    CpuProvider p(fr.root());
    sampleAt(p, 1);
    fr.write("proc/stat",
             "cpu  400 0 200 1400 0 0 0 0 0 0\n"
             "cpu0 10 0 100 600 0 0 0 0 0 0\n");
    auto f = sampleAt(p, 2);
    REQUIRE(f.cpu.logical.size() == 1);
    REQUIRE(f.cpu.logical[0].util.availability == Availability::Pending);
}

TEST_CASE("average cpu frequency", "[cpu]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "1500000\n");
    fr.write("sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq", "2500000\n");
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.freqHz.value == Approx(2.0e9));
    REQUIRE(f.cpu.freqHz.unit == Unit::Hertz);

    FakeRoot none;
    none.write("proc/stat", kStat0);
    CpuProvider q(none.root());
    REQUIRE(sampleAt(q, 1).cpu.freqHz.availability == Availability::Unsupported);
}

namespace {

void cpufreq(FakeRoot& fr, int cpu, const char* cur, const char* mn, const char* mx) {
    const std::string d = "sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/";
    fr.write(d + "scaling_cur_freq", cur);
    fr.write(d + "cpuinfo_min_freq", mn);
    fr.write(d + "cpuinfo_max_freq", mx);
}


}  // namespace

TEST_CASE("per-core frequency is read per core", "[cpu][freq]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    cpufreq(fr, 0, "2400000\n", "800000\n", "4700000\n");
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.logical[0].freqHz.availability == Availability::Measured);
    REQUIRE(*f.cpu.logical[0].freqHz.value == Approx(2.4e9));
    REQUIRE(*f.cpu.logical[0].freqMinHz.value == Approx(8e8));
    REQUIRE(*f.cpu.logical[0].freqMaxHz.value == Approx(4.7e9));
    REQUIRE(*f.cpu.freqHz.value == Approx(2.4e9));  // the average is unchanged
}

TEST_CASE("a core without cpufreq has unsupported frequency", "[cpu][freq]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    cpufreq(fr, 0, "2400000\n", "800000\n", "4700000\n");
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.logical[1].freqHz.availability == Availability::Unsupported);
    REQUIRE_FALSE(f.cpu.logical[1].freqHz.value.has_value());
    REQUIRE(f.cpu.logical[1].freqMaxHz.availability == Availability::Unsupported);
}

TEST_CASE("unreadable frequency is restricted", "[cpu][freq]") {
    if (::geteuid() == 0) SKIP("root can read mode-000 files");
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    cpufreq(fr, 0, "2400000\n", "800000\n", "4700000\n");
    fr.makeUnreadable("sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.logical[0].freqHz.availability == Availability::Restricted);
    REQUIRE_FALSE(f.cpu.logical[0].freqHz.value.has_value());
}

TEST_CASE("governor and epp are reported once", "[cpu][freq]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    fr.write("sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "powersave\n");
    fr.write("sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", "balance_performance\n");
    fr.write("sys/devices/system/cpu/cpu0/cpufreq/scaling_driver", "intel_pstate\n");
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.governor == "powersave");
    REQUIRE(f.cpu.epp == "balance_performance");
    REQUIRE(f.cpu.driver == "intel_pstate");

    FakeRoot bare;
    bare.write("proc/stat", kStat0);
    bare.write("proc/uptime", "1.0 1.0\n");
    CpuProvider q(bare.root());
    auto g = sampleAt(q, 1);
    REQUIRE(g.cpu.governor.empty());
    REQUIRE(g.cpu.epp.empty());
    REQUIRE(g.cpu.driver.empty());
}

TEST_CASE("throttle events are cumulative", "[cpu][freq]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    fr.write("sys/devices/system/cpu/cpu0/thermal_throttle/core_throttle_count", "3\n");
    fr.write("sys/devices/system/cpu/cpu1/thermal_throttle/core_throttle_count", "4\n");
    fr.write("sys/devices/system/cpu/cpu0/thermal_throttle/package_throttle_count", "1\n");
    fr.write("sys/devices/system/cpu/cpu1/thermal_throttle/package_throttle_count", "1\n");  // same package: counted once
    CpuProvider p(fr.root());
    auto f = sampleAt(p, 1);
    REQUIRE(f.cpu.throttleEvents.availability == Availability::Measured);
    REQUIRE(*f.cpu.throttleEvents.value == Approx(8.0));

    FakeRoot none;
    none.write("proc/stat", kStat0);
    none.write("proc/uptime", "1.0 1.0\n");
    CpuProvider q(none.root());
    REQUIRE(sampleAt(q, 1).cpu.throttleEvents.availability == Availability::Unsupported);
}

TEST_CASE("after a reset the first cpu rate is Pending again", "[cpu][minor]") {
    FakeRoot fr;
    fr.write("proc/stat", kStat0);
    fr.write("proc/uptime", "1.0 1.0\n");
    CpuProvider p(fr.root());
    sampleAt(p, 1);
    fr.write("proc/stat", kStat1);
    REQUIRE(sampleAt(p, 2).cpu.total.availability == Availability::Measured);
    p.reset();  // e.g. the machine slept: the next delta would span the whole sleep
    fr.write("proc/stat", kStat0);
    REQUIRE(sampleAt(p, 3).cpu.total.availability == Availability::Pending);
}
