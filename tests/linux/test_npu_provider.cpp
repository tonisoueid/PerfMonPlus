// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/npu_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;
using pmp::linux::NpuProvider;

namespace {
Frame sampleAt(NpuProvider& p, int gen, MonoTime t) {
    Frame f;
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), t});
    return f;
}
const MonoTime t0{};
}  // namespace

TEST_CASE("npu active time from the busy counter", "[npu]") {
    FakeRoot fr;
    fr.write("sys/class/accel/accel0/device/vendor", "0x8086\n");
    fr.symlink("../../../bus/pci/drivers/intel_vpu", "sys/class/accel/accel0/device/driver");
    fr.write("sys/class/accel/accel0/device/npu_busy_time_us", "0\n");
    NpuProvider p(fr.root());
    const auto first = sampleAt(p, 1, t0);
    REQUIRE(first.npus.size() == 1);
    REQUIRE(first.npus[0].name == "Intel NPU (intel_vpu)");
    REQUIRE(first.npus[0].activePct.availability == Availability::Pending);
    fr.write("sys/class/accel/accel0/device/npu_busy_time_us", "500000\n");
    REQUIRE(*sampleAt(p, 2, t0 + 1s).npus[0].activePct.value == Approx(50.0));
    fr.write("sys/class/accel/accel0/device/npu_busy_time_us", "9000000\n");
    REQUIRE(*sampleAt(p, 3, t0 + 2s).npus[0].activePct.value == Approx(100.0));  // clamped
}

TEST_CASE("no accel devices means no npu", "[npu]") {
    FakeRoot fr;
    NpuProvider p(fr.root());
    REQUIRE(sampleAt(p, 1, t0).npus.empty());
}

TEST_CASE("an npu without a busy counter is listed but unsupported", "[npu]") {
    FakeRoot fr;
    fr.makeDir("sys/class/accel/accel0/device");
    NpuProvider p(fr.root());
    const auto f = sampleAt(p, 1, t0);
    REQUIRE(f.npus.size() == 1);
    REQUIRE(f.npus[0].activePct.availability == Availability::Unsupported);
}
