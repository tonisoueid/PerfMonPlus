// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/power_freq_model.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using Catch::Approx;

namespace {

LogicalCpu core(int id, Metric cur, Metric mx) {
    LogicalCpu c;
    c.id = id;
    c.freqHz = cur;
    c.freqMaxHz = mx;
    c.freqMinHz = Metric::measured(8e8, Unit::Hertz);
    return c;
}

Metric hz(double v) { return Metric::measured(v, Unit::Hertz); }

}  // namespace

TEST_CASE("core frequencies format as GHz with a measured max", "[power]") {
    Frame f;
    f.cpu.logical.push_back(core(0, hz(2.4e9), hz(4.7e9)));
    const auto m = buildPowerFreq(f);
    REQUIRE(m.cores.size() == 1);
    REQUIRE(m.cores[0].label == "CPU 0");
    REQUIRE(m.cores[0].cur == "2.40 GHz");
    REQUIRE(m.cores[0].max == "4.70 GHz");
    REQUIRE(m.cores[0].min == "800.00 MHz");
    REQUIRE(m.cores[0].fraction == Approx(0.5106).margin(0.001));
    REQUIRE(m.cores[0].available);
}

TEST_CASE("unsupported frequency shows unavailable, not 0 GHz", "[power]") {
    Frame f;
    f.cpu.logical.push_back(core(0, Metric::notMeasured(Availability::Unsupported, Unit::Hertz),
                                 Metric::notMeasured(Availability::Unsupported, Unit::Hertz)));
    const auto m = buildPowerFreq(f);
    REQUIRE(m.cores[0].cur == "Unavailable");
    REQUIRE(m.cores[0].fraction == 0);
    REQUIRE_FALSE(m.cores[0].available);
}

TEST_CASE("throttle indicator", "[power]") {
    Frame f;
    f.cpu.throttleEvents = Metric::measured(8, Unit::Count);
    REQUIRE(buildPowerFreq(f).throttle == "8 events since boot");
    f.cpu.throttleEvents = Metric::measured(1, Unit::Count);
    REQUIRE(buildPowerFreq(f).throttle == "1 event since boot");
    f.cpu.throttleEvents = Metric::measured(0, Unit::Count);
    REQUIRE(buildPowerFreq(f).throttle == "None");
    f.cpu.throttleEvents = Metric::notMeasured(Availability::Unsupported, Unit::Count);
    REQUIRE(buildPowerFreq(f).throttle == "Not reported by this CPU");
}

TEST_CASE("governor and driver pass through, empty ones say so", "[power]") {
    Frame f;
    f.cpu.governor = "powersave";
    f.cpu.epp = "balance_performance";
    const auto m = buildPowerFreq(f);
    REQUIRE(m.governor == "powersave");
    REQUIRE(m.epp == "Balance performance");
    REQUIRE(m.driver == "Unavailable");
}

TEST_CASE("restricted domains explain why", "[power]") {
    Frame f;
    for (const char* id : {"package-0", "core"}) {
        PowerDomain d;
        d.id = id;
        d.label = id;
        d.watts = Metric::notMeasured(Availability::Restricted, Unit::Watts);
        f.energy.domains.push_back(d);
    }
    f.energy.restrictedPath = "/sys/class/powercap/intel-rapl:0/energy_uj";
    const auto m = buildPowerFreq(f);
    REQUIRE(m.domains.size() == 2);
    for (const auto& d : m.domains) {
        REQUIRE(d.value == "Restricted");
        REQUIRE(d.restricted);
    }
    REQUIRE(m.restrictedHelp.find("root only") != std::string::npos);
    REQUIRE(m.restrictedHelp.find("never installs") != std::string::npos);
}

TEST_CASE("a measured zero domain is 0 W, a pending one is a dash", "[power]") {
    Frame f;
    PowerDomain a, b;
    a.id = a.label = "dram";
    a.watts = Metric::measured(0.0, Unit::Watts);
    b.id = b.label = "core";
    b.watts = Metric::notMeasured(Availability::Pending, Unit::Watts);
    f.energy.domains = {a, b};
    const auto m = buildPowerFreq(f);
    REQUIRE(m.domains[0].value == "0 W");
    REQUIRE(m.domains[1].value == "—");
    REQUIRE_FALSE(m.domains[1].restricted);
}

TEST_CASE("battery health card", "[power]") {
    Frame f;
    auto& b = f.energy.battery;
    b.present = true;
    b.status = "Charging";
    b.healthPct = Metric::measured(90.0, Unit::Percent);
    b.fullWh = Metric::measured(45.0, Unit::WattHours);
    b.designWh = Metric::measured(50.0, Unit::WattHours);
    b.cycleCount = Metric::notMeasured(Availability::Unsupported, Unit::Count);
    b.rateW = Metric::measured(12.0, Unit::Watts);
    const auto rows = buildPowerFreq(f).battery.rows;
    auto find = [&](const std::string& k) {
        for (const auto& r : rows) {
            if (r.first == k) return r.second;
        }
        return std::string("<missing>");
    };
    REQUIRE(find("Health") == "90% of design capacity (45.0 / 50.0 Wh)");
    REQUIRE(find("Cycle count") == "Unavailable");
    REQUIRE(find("Rate") == "Charging at 12.0 W");
    b.status = "Discharging";
    b.rateW = Metric::measured(9.8, Unit::Watts);
    const auto rows2 = buildPowerFreq(f).battery.rows;
    for (const auto& r : rows2) {
        if (r.first == "Rate") REQUIRE(r.second == "Discharging at 9.8 W");
    }
}

TEST_CASE("no battery means no battery card", "[power]") {
    Frame f;
    REQUIRE(buildPowerFreq(f).battery.rows.empty());
}
