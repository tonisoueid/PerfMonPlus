// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/linux/memory_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using pmp::linux::MemoryProvider;

namespace {

constexpr const char* kMeminfo =
    "MemTotal:       16384000 kB\n"
    "MemFree:         2048000 kB\n"
    "MemAvailable:    8192000 kB\n"
    "Buffers:          512000 kB\n"
    "Cached:          4096000 kB\n"
    "SReclaimable:     256000 kB\n"
    "Committed_AS:   10240000 kB\n"
    "SwapTotal:             0 kB\n"
    "SwapFree:              0 kB\n";

void populate(FakeRoot& fr) {
    fr.write("proc/meminfo", kMeminfo);
    fr.write("proc/vmstat", "pgpgin 152435342\npgpgout 10076\n");
    fr.write("proc/pressure/memory",
             "some avg10=1.50 avg60=0.80 avg300=0.10 total=12345\n"
             "full avg10=0.25 avg60=0.10 avg300=0.00 total=99\n");
}

Frame sampleOnce(MemoryProvider& p) {
    Frame f;
    p.sample(f, SampleContext{1, MonoTime{}});
    return f;
}

}  // namespace

TEST_CASE("memory provider maps meminfo to bytes", "[memory]") {
    FakeRoot fr;
    populate(fr);
    MemoryProvider p(fr.root());
    auto f = sampleOnce(p);
    REQUIRE(f.memory.total.value == 16384000.0 * 1024);
    REQUIRE(f.memory.inUse.value == 8192000.0 * 1024);
    REQUIRE(f.memory.cached.value == 4352000.0 * 1024);
    REQUIRE(f.memory.swapUsed.isMeasured());
    REQUIRE(f.memory.swapUsed.value == 0.0);
    REQUIRE(format(f.memory.swapUsed) == "Zero KB");
    REQUIRE(f.memory.pressureSomeAvg10.value == 1.5);
    REQUIRE(f.memory.pageIns.value == 152435342.0);
    REQUIRE(f.memory.compressed.availability == Availability::Unsupported);
}

TEST_CASE("missing PSI and MemAvailable are Unsupported", "[memory]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/meminfo", "MemTotal:       16384000 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
    fr.remove("proc/pressure");
    MemoryProvider p(fr.root());
    auto f = sampleOnce(p);
    REQUIRE(f.memory.pressureSomeAvg10.availability == Availability::Unsupported);
    REQUIRE(f.memory.available.availability == Availability::Unsupported);
    REQUIRE(f.memory.inUse.availability == Availability::Unsupported);
    REQUIRE(f.memory.total.isMeasured());
}

TEST_CASE("memory provider fillUnavailable marks every field", "[memory]") {
    FakeRoot fr;
    MemoryProvider p(fr.root());
    Frame f;
    p.fillUnavailable(f, Availability::Failed);
    REQUIRE(f.memory.total.availability == Availability::Failed);
    REQUIRE(f.memory.pageOuts.availability == Availability::Failed);
}
