// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/thermal_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

#include <unistd.h>

using namespace pmp;
using Catch::Approx;
using pmp::linux::ThermalProvider;

namespace {

void layout(FakeRoot& fr, int packageMilli = 49000) {
    fr.write("sys/class/hwmon/hwmon5/name", "coretemp\n");
    fr.write("sys/class/hwmon/hwmon5/temp1_input", std::to_string(packageMilli) + "\n");
    fr.write("sys/class/hwmon/hwmon5/temp1_label", "Package id 0\n");
    fr.write("sys/class/hwmon/hwmon5/temp1_max", "100000\n");
    fr.write("sys/class/hwmon/hwmon5/temp1_crit", "100000\n");
    fr.write("sys/class/hwmon/hwmon5/temp2_input", "47000\n");
    fr.write("sys/class/hwmon/hwmon5/temp2_label", "Core 0\n");
    fr.write("sys/class/hwmon/hwmon3/name", "nvme\n");
    fr.write("sys/class/hwmon/hwmon3/temp1_input", "38850\n");
    fr.write("sys/class/hwmon/hwmon3/temp1_label", "Composite\n");
    fr.write("sys/class/thermal/thermal_zone6/type", "x86_pkg_temp\n");
    fr.write("sys/class/thermal/thermal_zone6/temp", "49000\n");
}

Frame sample(ThermalProvider& p) {
    Frame f;
    p.sample(f, SampleContext{1, MonoTime{}});
    return f;
}

const SensorReading* byId(const Frame& f, const std::string& id) {
    for (const auto& s : f.thermals.sensors)
        if (s.id == id) return &s;
    return nullptr;
}

}  // namespace

TEST_CASE("hotspot prefers the package sensor", "[thermal]") {
    FakeRoot fr;
    layout(fr);
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(*f.thermals.hotspot.value == Approx(49.0));
    REQUIRE(f.thermals.pressure == ThermalPressure::Nominal);
    REQUIRE(f.thermals.sensors.size() == 3);
    REQUIRE(byId(f, "hwmon:hwmon5:temp1")->label == "coretemp Package id 0");
    REQUIRE(byId(f, "hwmon:hwmon3:temp1")->label == "nvme Composite");
    REQUIRE(*byId(f, "hwmon:hwmon3:temp1")->celsius.value == Approx(38.85));
}

TEST_CASE("pressure follows the sensor thresholds", "[thermal]") {
    FakeRoot fr;
    layout(fr, 92000);
    ThermalProvider p(fr.root());
    REQUIRE(sample(p).thermals.pressure == ThermalPressure::Moderate);
    fr.write("sys/class/hwmon/hwmon5/temp1_input", "96000\n");
    REQUIRE(sample(p).thermals.pressure == ThermalPressure::Moderate);
    fr.write("sys/class/hwmon/hwmon5/temp1_input", "100000\n");
    REQUIRE(sample(p).thermals.pressure == ThermalPressure::Critical);
}

TEST_CASE("falls back to the x86 package thermal zone", "[thermal]") {
    FakeRoot fr;
    layout(fr);
    fr.remove("sys/class/hwmon/hwmon5");
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(*f.thermals.hotspot.value == Approx(49.0));
    REQUIRE(f.thermals.pressure == ThermalPressure::Unknown);
}

TEST_CASE("a sensor read error is per-sensor", "[thermal]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    layout(fr);
    fr.makeUnreadable("sys/class/hwmon/hwmon3/temp1_input");
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(byId(f, "hwmon:hwmon3:temp1")->celsius.availability == Availability::Restricted);
    REQUIRE(byId(f, "hwmon:hwmon5:temp1")->celsius.isMeasured());
    REQUIRE(f.thermals.hotspot.isMeasured());
}

TEST_CASE("a vanished sensor file is omitted", "[thermal]") {
    FakeRoot fr;
    layout(fr);
    fr.remove("sys/class/hwmon/hwmon3/temp1_input");
    ThermalProvider p(fr.root());
    REQUIRE(sample(p).thermals.sensors.size() == 2);
}

TEST_CASE("fans are reported in rpm", "[thermal]") {
    FakeRoot fr;
    layout(fr);
    fr.write("sys/class/hwmon/hwmon7/name", "thinkpad\n");
    fr.write("sys/class/hwmon/hwmon7/fan1_input", "2400\n");
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(f.thermals.fans.size() == 1);
    REQUIRE(*f.thermals.fans[0].rpm.value == 2400.0);
    REQUIRE(f.thermals.fans[0].rpm.unit == Unit::Rpm);
}

TEST_CASE("a machine with no thermal hardware", "[thermal]") {
    FakeRoot fr;
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(f.thermals.hotspot.availability == Availability::Unsupported);
    REQUIRE(f.thermals.pressure == ThermalPressure::Unknown);
    REQUIRE(f.thermals.sensors.empty());
}

TEST_CASE("two sensors with the same chip name keep separate ids", "[thermal]") {
    FakeRoot fr;
    fr.write("sys/class/hwmon/hwmon3/name", "nvme\n");
    fr.write("sys/class/hwmon/hwmon3/temp1_input", "38000\n");
    fr.write("sys/class/hwmon/hwmon4/name", "nvme\n");
    fr.write("sys/class/hwmon/hwmon4/temp1_input", "52000\n");
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    std::set<std::string> ids;
    for (const auto& s : f.thermals.sensors) ids.insert(s.id);
    REQUIRE(ids.size() == 2);
}

TEST_CASE("a later package replaces a hotspot whose first read failed", "[thermal][minor]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    for (auto [hw, milli] : {std::pair{"hwmon1", 50000}, {"hwmon2", 61000}}) {  // two sockets
        fr.write(std::string("sys/class/hwmon/") + hw + "/name", "coretemp\n");
        fr.write(std::string("sys/class/hwmon/") + hw + "/temp1_input", std::to_string(milli) + "\n");
        fr.write(std::string("sys/class/hwmon/") + hw + "/temp1_label", "Package id 0\n");
    }
    fr.makeUnreadable("sys/class/hwmon/hwmon1/temp1_input");  // the first package cannot be read
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(f.thermals.hotspot.isMeasured());  // the readable package is used instead of reporting nothing
    REQUIRE(*f.thermals.hotspot.value == 61.0);
}

TEST_CASE("a board without the usual CPU sensors still has a hotspot", "[thermal][minor]") {
    FakeRoot fr;
    fr.write("sys/class/hwmon/hwmon0/name", "cpu_thermal\n");  // typical of ARM boards and some laptops
    fr.write("sys/class/hwmon/hwmon0/temp1_input", "55000\n");
    fr.write("sys/class/hwmon/hwmon1/name", "nvme\n");
    fr.write("sys/class/hwmon/hwmon1/temp1_input", "70000\n");  // a drive is not the hotspot
    ThermalProvider p(fr.root());
    const auto f = sample(p);
    REQUIRE(f.thermals.hotspot.isMeasured());
    REQUIRE(*f.thermals.hotspot.value == 55.0);
}
