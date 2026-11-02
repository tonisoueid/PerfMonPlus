// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/energy_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;
using pmp::linux::EnergyProvider;

namespace {

void battery(FakeRoot& fr, const std::string& status, bool acOnline) {
    fr.write("sys/class/power_supply/ADP1/type", "Mains\n");
    fr.write("sys/class/power_supply/ADP1/online", acOnline ? "1\n" : "0\n");
    fr.write("sys/class/power_supply/CMB0/type", "Battery\n");
    fr.write("sys/class/power_supply/CMB0/status", status + "\n");
    fr.write("sys/class/power_supply/CMB0/power_now", "9753000\n");
    fr.write("sys/class/power_supply/CMB0/energy_now", "57810000\n");
    fr.write("sys/class/power_supply/CMB0/energy_full", "64800000\n");
    fr.write("sys/class/power_supply/CMB0/energy_full_design", "80000000\n");
    fr.write("sys/class/power_supply/CMB0/cycle_count", "58\n");
    fr.write("sys/class/power_supply/CMB0/capacity", "89\n");
}

Frame sampleAt(EnergyProvider& p, int gen, MonoTime t) {
    Frame f;
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), t});
    return f;
}

const MonoTime t0{};

}  // namespace

TEST_CASE("a discharging battery drives watts", "[energy]") {
    if (geteuid() == 0) SKIP("root can read RAPL");
    FakeRoot fr;
    battery(fr, "Discharging", false);
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "1\n");
    fr.makeUnreadable("sys/class/powercap/intel-rapl:0/energy_uj");
    EnergyProvider p(fr.root());
    const auto f = sampleAt(p, 1, t0);
    const auto& e = f.energy;
    REQUIRE(e.source == PowerSource::Battery);
    REQUIRE(*e.watts.value == Approx(9.753));
    REQUIRE(e.wattsSource == WattsSource::BatteryDischarge);
    REQUIRE(e.restrictedPath.empty());
    REQUIRE(e.battery.present);
    REQUIRE(*e.battery.healthPct.value == Approx(81.0));
    REQUIRE(*e.battery.cycleCount.value == 58);
    REQUIRE(*e.battery.chargePct.value == 89);
    REQUIRE(*e.battery.timeRemaining.value == Approx(57.81 / 9.753 * 3600));
    REQUIRE_FALSE(e.battery.timeIsToFull);
}

TEST_CASE("charging rate is not consumption", "[energy]") {
    if (geteuid() == 0) SKIP("root can read RAPL");
    FakeRoot fr;
    battery(fr, "Charging", true);
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "1\n");
    fr.makeUnreadable("sys/class/powercap/intel-rapl:0/energy_uj");
    EnergyProvider p(fr.root());
    const auto e = sampleAt(p, 1, t0).energy;
    REQUIRE(e.source == PowerSource::AC);
    REQUIRE(e.watts.availability == Availability::Restricted);
    REQUIRE(e.restrictedPath.find("intel-rapl:0/energy_uj") != std::string::npos);
    REQUIRE(e.battery.timeIsToFull);
    REQUIRE(*e.battery.timeRemaining.value == Approx((64.8 - 57.81) / 9.753 * 3600));
}

TEST_CASE("a readable RAPL counter gives package watts", "[energy]") {
    FakeRoot fr;
    battery(fr, "Discharging", false);
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "1000000\n");
    EnergyProvider p(fr.root());
    REQUIRE(sampleAt(p, 1, t0).energy.watts.availability == Availability::Pending);
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "11000000\n");
    const auto e = sampleAt(p, 2, t0 + 1s).energy;
    REQUIRE(*e.watts.value == Approx(10.0));
    REQUIRE(e.wattsSource == WattsSource::CpuPackage);
}

TEST_CASE("the RAPL counter wraps", "[energy]") {
    FakeRoot fr;
    fr.write("sys/class/powercap/intel-rapl:0/max_energy_range_uj", "20000000\n");
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "19000000\n");
    EnergyProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "1000000\n");
    REQUIRE(*sampleAt(p, 2, t0 + 1s).energy.watts.value == Approx(2.0));
}

TEST_CASE("power mode comes from the platform profile or the cpu preference", "[energy]") {
    FakeRoot fr;
    fr.write("sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", "balance_power\n");
    EnergyProvider p(fr.root());
    REQUIRE(sampleAt(p, 1, t0).energy.powerMode == "Balance power");
    fr.write("sys/firmware/acpi/platform_profile", "performance\n");
    REQUIRE(sampleAt(p, 2, t0 + 1s).energy.powerMode == "Performance");
}

TEST_CASE("a desktop without battery or RAPL", "[energy]") {
    FakeRoot fr;
    fr.write("sys/class/power_supply/AC/type", "Mains\n");
    fr.write("sys/class/power_supply/AC/online", "1\n");
    EnergyProvider p(fr.root());
    const auto e = sampleAt(p, 1, t0).energy;
    REQUIRE(e.source == PowerSource::AC);
    REQUIRE(e.watts.availability == Availability::Unsupported);
    REQUIRE_FALSE(e.battery.present);
}

namespace {

void rapl(FakeRoot& fr, const std::string& dir, const std::string& name, const std::string& uj) {
    fr.write("sys/class/powercap/" + dir + "/name", name + "\n");
    fr.write("sys/class/powercap/" + dir + "/energy_uj", uj + "\n");
    fr.write("sys/class/powercap/" + dir + "/max_energy_range_uj", "262143328850\n");
}

const Metric* domainWatts(const EnergySnapshot& e, const std::string& id) {
    for (const auto& d : e.domains) {
        if (d.id == id) return &d.watts;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("rapl domains are listed with their labels", "[energy][power]") {
    FakeRoot fr;
    rapl(fr, "intel-rapl:0", "package-0", "1000000");
    rapl(fr, "intel-rapl:0:0", "core", "500000");
    rapl(fr, "intel-rapl:0:1", "uncore", "100000");
    rapl(fr, "intel-rapl:0:2", "dram", "200000");
    rapl(fr, "intel-rapl:1", "psys", "3000000");
    EnergyProvider p(fr.root());
    const auto first = sampleAt(p, 1, t0).energy;
    REQUIRE(first.domains.size() == 5);
    REQUIRE(first.domains[0].id == "package-0");
    REQUIRE(first.domains[0].label == "Package");
    REQUIRE(first.domains[1].label == "CPU cores");
    REQUIRE(first.domains[2].label == "Uncore / iGPU");
    REQUIRE(first.domains[3].label == "Memory (DRAM)");
    REQUIRE(first.domains[4].label == "Platform");
    for (const auto& d : first.domains) REQUIRE(d.watts.availability == Availability::Pending);

    rapl(fr, "intel-rapl:0", "package-0", "6000000");  // +5,000,000 uJ in 1 s = 5 W
    rapl(fr, "intel-rapl:0:0", "core", "3500000");     // +3 W
    const auto second = sampleAt(p, 2, t0 + 1s).energy;
    REQUIRE(*domainWatts(second, "package-0")->value == Approx(5.0));
    REQUIRE(*domainWatts(second, "core")->value == Approx(3.0));
    REQUIRE(*domainWatts(second, "dram")->value == Approx(0.0));  // a measured zero stays a zero
    REQUIRE(domainWatts(second, "dram")->availability == Availability::Measured);
}

TEST_CASE("a rapl domain counter that wrapped is still correct", "[energy][power]") {
    FakeRoot fr;
    rapl(fr, "intel-rapl:0", "package-0", "262143000000");
    EnergyProvider p(fr.root());
    sampleAt(p, 1, t0);
    rapl(fr, "intel-rapl:0", "package-0", "2000000");  // wrapped past 262143328850
    const auto e = sampleAt(p, 2, t0 + 1s).energy;
    REQUIRE(*domainWatts(e, "package-0")->value == Approx((262143328850.0 - 262143000000.0 + 2000000.0) / 1e6));
}

TEST_CASE("unreadable rapl domains are restricted with the path", "[energy][power]") {
    if (geteuid() == 0) SKIP("root can read RAPL");
    FakeRoot fr;
    rapl(fr, "intel-rapl:0", "package-0", "1");
    rapl(fr, "intel-rapl:0:0", "core", "1");
    fr.makeUnreadable("sys/class/powercap/intel-rapl:0/energy_uj");
    fr.makeUnreadable("sys/class/powercap/intel-rapl:0:0/energy_uj");
    EnergyProvider p(fr.root());
    const auto e = sampleAt(p, 1, t0).energy;
    REQUIRE(e.domains.size() == 2);
    for (const auto& d : e.domains) {
        REQUIRE(d.watts.availability == Availability::Restricted);
        REQUIRE_FALSE(d.watts.value.has_value());
    }
    REQUIRE(e.restrictedPath.find("intel-rapl:0/energy_uj") != std::string::npos);
}

TEST_CASE("battery detail", "[energy][power]") {
    FakeRoot fr;
    battery(fr, "Discharging", false);
    fr.write("sys/class/power_supply/CMB0/energy_full_design", "50000000\n");
    fr.write("sys/class/power_supply/CMB0/energy_full", "45000000\n");
    fr.write("sys/class/power_supply/CMB0/energy_now", "20000000\n");
    fr.write("sys/class/power_supply/CMB0/voltage_now", "12000000\n");
    fr.write("sys/class/power_supply/CMB0/temp", "312\n");
    EnergyProvider p(fr.root());
    const auto b = sampleAt(p, 1, t0).energy.battery;
    REQUIRE(*b.designWh.value == Approx(50.0));
    REQUIRE(*b.fullWh.value == Approx(45.0));
    REQUIRE(*b.currentWh.value == Approx(20.0));
    REQUIRE(*b.voltageV.value == Approx(12.0));
    REQUIRE(*b.temperatureC.value == Approx(31.2));
    REQUIRE(*b.healthPct.value == Approx(90.0));

    fr.remove("sys/class/power_supply/CMB0/temp");
    EnergyProvider q(fr.root());
    REQUIRE(sampleAt(q, 1, t0).energy.battery.temperatureC.availability == Availability::Unsupported);
}

TEST_CASE("a rapl counter that cannot be understood is Failed, not Unsupported", "[energy][minor]") {
    FakeRoot fr;
    fr.write("sys/class/powercap/intel-rapl:0/energy_uj", "garbage\n");
    EnergyProvider p(fr.root());
    const auto e = sampleAt(p, 1, t0).energy;
    REQUIRE(e.watts.availability == Availability::Failed);  // it exists but gave nonsense: that is a fault, not an absence
    FakeRoot none;
    EnergyProvider q(none.root());
    REQUIRE(sampleAt(q, 1, t0).energy.watts.availability == Availability::Unsupported);  // no counter at all
}

TEST_CASE("a battery that reports energy and current gets its time from watts", "[energy][minor]") {
    FakeRoot fr;
    fr.write("sys/class/power_supply/BAT0/type", "Battery\n");
    fr.write("sys/class/power_supply/BAT0/status", "Discharging\n");
    fr.write("sys/class/power_supply/BAT0/energy_now", "30000000\n");       // 30 Wh in µWh
    fr.write("sys/class/power_supply/BAT0/energy_full", "60000000\n");
    fr.write("sys/class/power_supply/BAT0/current_now", "1500000\n");        // 1.5 A in µA (no power_now file)
    fr.write("sys/class/power_supply/BAT0/voltage_now", "10000000\n");       // 10 V in µV
    EnergyProvider p(fr.root());
    const auto b = sampleAt(p, 1, t0).energy.battery;
    REQUIRE(*b.rateW.value == Approx(15.0));
    // 30 Wh at 15 W is two hours: dividing µWh by µA would have given nonsense
    REQUIRE(*b.timeRemaining.value == Approx(2.0 * 3600.0));
}

TEST_CASE("on a two-socket machine the domains say which socket they belong to", "[energy][minor]") {
    FakeRoot fr;
    rapl(fr, "intel-rapl:0", "package-0", "1000");
    rapl(fr, "intel-rapl:0:0", "core", "1000");
    rapl(fr, "intel-rapl:1", "package-1", "1000");
    rapl(fr, "intel-rapl:1:0", "core", "1000");
    rapl(fr, "intel-rapl:2", "psys", "1000");  // only one: no suffix needed
    EnergyProvider p(fr.root());
    const auto e = sampleAt(p, 1, t0).energy;
    std::map<std::string, std::string> labelOf;
    for (const auto& d : e.domains) labelOf[d.id] = d.label;
    REQUIRE(labelOf["package-0"] == "Package (socket 0)");
    REQUIRE(labelOf["package-1"] == "Package (socket 1)");
    REQUIRE(labelOf["core"] == "CPU cores (socket 0)");
    REQUIRE(labelOf["core@1:0"] == "CPU cores (socket 1)");
    REQUIRE(labelOf["psys"] == "Platform");
}
