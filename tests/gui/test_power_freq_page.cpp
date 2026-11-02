// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/gui/power_freq_page.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::gui;

namespace {

Frame frameAt(int sec, bool freqSupported = true) {
    Frame f;
    f.generation = static_cast<std::uint64_t>(sec + 1);
    f.mono = MonoTime{} + std::chrono::seconds(sec);
    for (int i = 0; i < 4; ++i) {
        LogicalCpu c;
        c.id = i;
        c.coreClass = i < 2 ? CoreClass::Performance : CoreClass::Efficiency;
        c.util = Metric::measured(10, Unit::Percent);
        if (freqSupported || i != 3) {
            c.freqHz = Metric::measured(2.4e9, Unit::Hertz);
            c.freqMaxHz = Metric::measured(4.7e9, Unit::Hertz);
        } else {
            c.freqHz = Metric::notMeasured(Availability::Unsupported, Unit::Hertz);
            c.freqMaxHz = c.freqHz;
        }
        f.cpu.logical.push_back(c);
    }
    f.cpu.governor = "powersave";
    f.cpu.throttleEvents = Metric::measured(0, Unit::Count);
    PowerDomain d;
    d.id = "package-0";
    d.label = "Package";
    d.watts = Metric::measured(5.0, Unit::Watts);
    f.energy.domains.push_back(d);
    return f;
}

}  // namespace

TEST_CASE("page shows one graph per core and updates from a frame", "[gui][power]") {
    PowerFreqPage page;
    page.resize(900, 700);
    HistoryStore h(60);
    for (int i = 0; i < 3; ++i) h.ingest(frameAt(i), false);
    page.update(frameAt(2), h, MonoTime{} + std::chrono::seconds(2));
    REQUIRE(page.frequencyGraphs().size() == 4);
    REQUIRE(page.powerGraphs().size() == 1);
    REQUIRE(page.governorText() == "powersave");
    REQUIRE(page.throttleText() == "None");
}

TEST_CASE("an unsupported core says so and is not drawn as zero", "[gui][power]") {
    PowerFreqPage page;
    HistoryStore h(60);
    for (int i = 0; i < 3; ++i) h.ingest(frameAt(i, false), false);
    page.update(frameAt(2, false), h, MonoTime{} + std::chrono::seconds(2));
    REQUIRE(page.frequencyGraphs()[3]->accessibleName() == "CPU 3 frequency unavailable");
    const auto& ring = h.logicalFreq(3);
    for (std::size_t i = 0; i < ring.size(); ++i) REQUIRE_FALSE(ring.at(i).value.has_value());
}

TEST_CASE("graphs carry accessible names", "[gui][power]") {
    PowerFreqPage page;
    HistoryStore h(60);
    h.ingest(frameAt(0), false);
    page.update(frameAt(0), h, MonoTime{});
    REQUIRE(page.frequencyGraphs()[0]->accessibleName() == "CPU 0 frequency 2.40 gigahertz");
    REQUIRE(page.powerGraphs()[0]->accessibleName() == "Package power 5 W");
}

TEST_CASE("restricted power shows the help text, plain", "[gui][power]") {
    PowerFreqPage page;
    HistoryStore h(60);
    auto f = frameAt(0);
    f.energy.domains[0].watts = Metric::notMeasured(Availability::Restricted, Unit::Watts);
    f.energy.restrictedPath = "/sys/<b>x</b>";
    h.ingest(f, false);
    page.update(f, h, MonoTime{});
    REQUIRE(page.helpText().contains("root only"));
    REQUIRE(page.helpText().contains("<b>x</b>"));
}

TEST_CASE("the page does not animate", "[gui][power]") {
    PowerFreqPage page;
    REQUIRE_FALSE(page.animates());
}

TEST_CASE("battery card appears only with a battery", "[gui][power]") {
    PowerFreqPage page;
    HistoryStore h(60);
    auto f = frameAt(0);
    h.ingest(f, false);
    page.update(f, h, MonoTime{});
    REQUIRE(page.batteryText().isEmpty());
    f.energy.battery.present = true;
    f.energy.battery.status = "Discharging";
    f.energy.battery.healthPct = Metric::measured(90, Unit::Percent);
    page.update(f, h, MonoTime{});
    REQUIRE(page.batteryText().contains("Health: 90% of design capacity"));
}
