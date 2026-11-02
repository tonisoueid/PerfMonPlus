// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/hardware_model.hpp>
#include <perfmonplus/vm/summary_model.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::testing;

TEST_CASE("energy tile text", "[hardware]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto t = energyTile(f);
    REQUIRE(t.title == "Energy");
    REQUIRE(t.value == "10 W");
    REQUIRE(t.subtitle == "Thermals nominal · Battery 89%");
    REQUIRE(t.available);
}

TEST_CASE("a restricted power counter reads Restricted", "[hardware]") {
    auto f = populatedFrame();
    withHardware(f);
    f.energy.watts = Metric::notMeasured(Availability::Restricted, Unit::Watts);
    f.energy.restrictedPath = "/sys/class/powercap/intel-rapl:0/energy_uj";
    const auto t = energyTile(f);
    REQUIRE(t.value == "Restricted");
    REQUIRE_FALSE(t.available);
}

TEST_CASE("thermals tile text", "[hardware]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto t = thermalsTile(f);
    REQUIRE(t.title == "Thermals");
    REQUIRE(t.value == "49.0 °C");
    REQUIRE(t.subtitle == "Nominal thermal pressure");
    REQUIRE(pressureText(ThermalPressure::Unknown) == "Unavailable");
}

TEST_CASE("gpu and npu tiles", "[hardware]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto g = gpuTile(f.gpus[0]);
    REQUIRE(g.title == "GPU 0");
    REQUIRE(g.value == "18.0%");
    REQUIRE(g.subtitle == "Intel Iris Xe Graphics");
    NpuSnapshot n;
    n.index = 1;
    n.name = "Intel NPU (intel_vpu)";
    n.activePct = Metric::measured(50, Unit::Percent);
    REQUIRE(npuTile(n).title == "NPU 1");
    REQUIRE(npuTile(n).value == "50.0%");
}

TEST_CASE("power source text", "[hardware]") {
    EnergySnapshot e;
    REQUIRE(powerSourceText(e) == "Unavailable");
    e.source = PowerSource::AC;
    REQUIRE(powerSourceText(e) == "AC Power");
    e.source = PowerSource::Battery;
    e.battery.chargePct = Metric::measured(89.4, Unit::Percent);
    REQUIRE(powerSourceText(e) == "Battery 89%");
}

TEST_CASE("vfd columns are measured when the hardware reports", "[summary]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto vfd = vfdStack(f, 4.7e9);
    REQUIRE(vfd[1].measured);
    REQUIRE(vfd[1].value == "1.50 GHz");
    REQUIRE(vfd[2].value == "49.0 °C");
    REQUIRE(vfd[3].value == "18.0%");
    REQUIRE(vfd[1].fraction == Catch::Approx(1.5 / 4.7));
}

TEST_CASE("battery stats", "[hardware]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto s = batteryStats(f.energy);
    auto find = [&](const std::string& k) {
        for (const auto& i : s)
            if (i.caption == k) return i.value;
        return std::string("<missing>");
    };
    REQUIRE(find("Charge") == "89.0%");
    REQUIRE(find("Time remaining") == "0:05:55:39");
    REQUIRE(find("Health") == "81.0%");
    REQUIRE(find("Cycle count") == "58");
    REQUIRE(find("Rate") == "10 W");
    f.energy.battery.timeIsToFull = true;
    bool toFull = false, remaining = false;
    for (const auto& i : batteryStats(f.energy)) {
        toFull = toFull || i.caption == "Time to full";
        remaining = remaining || i.caption == "Time remaining";
    }
    REQUIRE(toFull);
    REQUIRE_FALSE(remaining);
    f.energy.battery.present = false;
    REQUIRE(batteryStats(f.energy).empty());
}

TEST_CASE("energy chips and thermal chips", "[hardware]") {
    auto f = populatedFrame();
    withHardware(f);
    const auto c = energyChips(f);
    REQUIRE(c.size() == 3);
    REQUIRE(c[0].value == "Nominal");
    REQUIRE(c[1].value == "Balance power");
    REQUIRE(c[2].value == "Battery 89%");
    const auto t = thermalChips(f);
    REQUIRE(t[0].value == "Nominal");
    REQUIRE(t[1].value == "49.0 °C");
}

namespace {
Frame demandFrame(WattsSource src) {
    Frame f;
    f.energy.wattsSource = src;
    f.energy.watts = Metric::measured(20, Unit::Watts);
    for (auto [pid, cpu] : {std::pair{1, 10.0}, {2, 30.0}}) {
        ProcessRecord r;
        r.key = {pid, static_cast<std::uint64_t>(pid)};
        r.name = "p" + std::to_string(pid);
        r.cpu = Metric::measured(cpu, Unit::Percent);
        f.processes.processes.push_back(r);
    }
    ProcessRecord pending;
    pending.key = {3, 3};
    pending.name = "pending";
    f.processes.processes.push_back(pending);
    return f;
}
}  // namespace

TEST_CASE("energy demand by cpu share of package power", "[hardware]") {
    const auto rows = energyDemand(demandFrame(WattsSource::CpuPackage));
    REQUIRE(rows.size() == 2);  // the process with no measured cpu is left out
    REQUIRE(rows[0].name == "p2");
    REQUIRE(rows[0].value == "15 W");
    REQUIRE(rows[0].score == Catch::Approx(100.0));
    REQUIRE(rows[1].value == "5 W");
    REQUIRE(rows[1].score == Catch::Approx(100.0 / 3.0));
    REQUIRE(energyFootnote(demandFrame(WattsSource::CpuPackage)).find("package power") != std::string::npos);
}

TEST_CASE("energy demand falls back to relative cpu use", "[hardware]") {
    const auto f = demandFrame(WattsSource::BatteryDischarge);
    const auto rows = energyDemand(f);
    REQUIRE(rows[0].score == Catch::Approx(100.0));
    REQUIRE(rows[0].value == "100");
    REQUIRE(energyFootnote(f).find("does not publish per-process energy") != std::string::npos);
    REQUIRE(energyDemand(f, 1).size() == 1);
}

TEST_CASE("restricted power help names the path", "[hardware]") {
    EnergySnapshot e;
    REQUIRE(restrictedPowerHelp(e).empty());
    e.restrictedPath = "/sys/class/powercap/intel-rapl:0/energy_uj";
    const auto text = restrictedPowerHelp(e);
    REQUIRE(text.find("intel-rapl:0/energy_uj") != std::string::npos);
    REQUIRE(text.find("root only") != std::string::npos);
}
