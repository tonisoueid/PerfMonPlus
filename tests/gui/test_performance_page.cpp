// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/performance_page.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::testing;

namespace {

void feed(PerformancePage& page, HistoryStore& h, const Frame& f) {
    h.ingest(f, false);
    page.update(f, h, f.mono);
}

}  // namespace

TEST_CASE("selecting a master card switches the detail", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    feed(page, h, populatedFrame());
    REQUIRE(page.detailTitle() == "CPU");
    page.masterCards().at(1)->click();
    REQUIRE(page.detailTitle() == "Memory");
    page.masterCards().at(3)->click();
    REQUIRE(page.detailTitle() == "Network");
}

TEST_CASE("cpu detail has one mini graph per logical cpu", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    page.resize(900, 600);
    feed(page, h, populatedFrame(8));
    REQUIRE(page.cpuCells().size() == 8);
    REQUIRE(page.cpuCells().at(3)->property("cpu").toInt() == 3);
}

TEST_CASE("64 logical processors fit the viewport at minimum window size", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    page.resize(690, 480);
    page.show();
    QApplication::processEvents();
    feed(page, h, populatedFrame(64));
    QApplication::processEvents();
    REQUIRE(page.cpuCells().size() == 64);
    const int gridWidth = page.cpuGridWidth();
    REQUIRE(gridWidth > 0);
    for (auto* cell : page.cpuCells()) {
        REQUIRE(cell->width() > 0);
        REQUIRE(cell->geometry().right() <= gridWidth);
    }
}

TEST_CASE("core class tints the tile", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame(2);
    f.cpu.logical[0].coreClass = CoreClass::Performance;
    f.cpu.logical[1].coreClass = CoreClass::Efficiency;
    feed(page, h, f);
    REQUIRE(page.cpuCells().at(0)->property("coreClass").toString() == "performance");
    REQUIRE(page.cpuCells().at(1)->property("coreClass").toString() == "efficiency");
}

TEST_CASE("first frame shows dashes, not zeros, on every card", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    feed(page, h, Frame{});
    for (const auto* label : page.findChildren<QLabel*>()) {
        REQUIRE_FALSE(label->text().contains("0.0%"));
        REQUIRE_FALSE(label->text().contains("Zero"));
    }
    REQUIRE(page.cpuCells().empty());
}

TEST_CASE("memory detail lists the stat blocks", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    feed(page, h, populatedFrame());
    page.masterCards().at(1)->click();
    REQUIRE(page.statValue("In use") == "66.29 GB");
    REQUIRE(page.statValue("Swap used") == "Zero KB");
}

TEST_CASE("selection survives a hardware change", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    feed(page, h, populatedFrame());
    page.masterCards().at(3)->click();  // Network
    REQUIRE(page.detailTitle() == "Network");
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    REQUIRE(page.masterCards().size() == 7);
    REQUIRE(page.detailTitle() == "Network");
}

TEST_CASE("the gpu panel opens by subsystem", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Gpu, 0);
    REQUIRE(page.detailTitle() == "GPU 0");
    REQUIRE(page.statValue("Driver") == "i915");
}

TEST_CASE("a partial gpu total is labelled", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    f.gpus[0].visibleClientsOnly = true;
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Gpu, 0);
    bool found = false;
    for (const auto* label : page.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&page) && label->text().contains("other users")) found = true;
    }
    REQUIRE(found);
    f.gpus[0].visibleClientsOnly = false;
    feed(page, h, f);
    for (const auto* label : page.findChildren<QLabel*>()) {
        REQUIRE_FALSE((label->isVisibleTo(&page) && label->text().contains("other users")));
    }
}

TEST_CASE("hardware that disappears removes its card", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Gpu, 0);
    feed(page, h, populatedFrame());
    REQUIRE(page.masterCards().size() == 4);
    REQUIRE(page.detailTitle() == "CPU");  // the selected panel went away: fall back to CPU
}

TEST_CASE("the thermals panel has one tile per sensor", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Thermals);
    REQUIRE(page.detailTitle() == "Thermals");
    REQUIRE(page.sensorTiles().size() == 3);
}

TEST_CASE("an empty sensor list says so", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    f.thermals.sensors.clear();
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Thermals);
    bool found = false;
    for (const auto* label : page.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&page) && label->text().contains("No sensors available")) found = true;
    }
    REQUIRE(found);
}

TEST_CASE("a restricted power counter explains how to grant access", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    f.energy.watts = Metric::notMeasured(Availability::Restricted, Unit::Watts);
    f.energy.restrictedPath = "/sys/class/powercap/intel-rapl:0/energy_uj";
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Energy);
    bool found = false;
    for (const auto* label : page.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&page) && label->text().contains("root only")) found = true;
    }
    REQUIRE(found);
}

TEST_CASE("the energy panel lists battery facts and demand", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    page.selectSubsystem(vm::Subsystem::Energy);
    REQUIRE(page.statValue("Health") == "81.0%");
    REQUIRE(page.statValue("Cycle count") == "58");
}

TEST_CASE("cpu facts appear on the cpu panel once known", "[gui][performance]") {
    PerformancePage page;
    HistoryStore h;
    linux::SystemInfo info;
    info.cpu.l3 = Metric::measured(12288.0 * 1024, Unit::Bytes);
    info.cpu.baseHz = Metric::measured(2.8e9, Unit::Hertz);
    info.cpu.driver = "intel_pstate";
    page.setSystemInfo(info);
    feed(page, h, populatedFrame());
    REQUIRE(page.statValue("L3 cache") == "12 MB");
    REQUIRE(page.statValue("Base speed") == "2.80 GHz");
    REQUIRE(page.statValue("Frequency driver") == "intel_pstate");
    REQUIRE(page.statValue("Utilization") == "15.8%");  // the live stats are still there
}
