// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/summary_page.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>
#include <QTableWidget>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::testing;

namespace {

void feed(SummaryPage& page, HistoryStore& h, const Frame& f) {
    h.ingest(f, false);
    page.update(f, h, f.mono);
}

}  // namespace

TEST_CASE("summary page renders a populated frame", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    page.resize(1000, 640);
    feed(page, h, populatedFrame(8, 15));
    REQUIRE(page.cpuValueText() == "15.8%");
    REQUIRE(page.processTable()->rowCount() == 12);
    REQUIRE(page.processTable()->item(0, 1)->text() == "proc15");
}

TEST_CASE("summary page shows no zeros on the first frame", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    page.resize(1000, 640);
    feed(page, h, Frame{});
    for (const auto* label : page.findChildren<QLabel*>()) {
        REQUIRE_FALSE(label->text().contains("0.0%"));
        REQUIRE_FALSE(label->text().contains("Zero"));
    }
    REQUIRE(page.cpuMeter()->litCount() == 0);
    REQUIRE(page.processTable()->rowCount() == 0);
}

TEST_CASE("process names are elided", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    page.resize(1000, 640);
    auto f = populatedFrame(8, 3);
    f.processes.processes[0].name = std::string(200, 'x');
    const int widthBefore = page.processTable()->columnWidth(1);
    feed(page, h, f);
    bool elided = false;
    for (int r = 0; r < page.processTable()->rowCount(); ++r) {
        if (page.processTable()->item(r, 1)->text().endsWith("…")) elided = true;
    }
    REQUIRE(elided);
    REQUIRE(page.processTable()->columnWidth(1) == widthBefore);
}

TEST_CASE("legend toggles hide a series", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    feed(page, h, populatedFrame());
    REQUIRE(page.cpuGraph()->seriesCount() == 3);
    page.legendButton(1)->click();  // Kernel
    REQUIRE(page.cpuGraph()->seriesCount() == 2);
    page.legendButton(1)->click();
    REQUIRE(page.cpuGraph()->seriesCount() == 3);
    page.legendButton(2)->click();  // Temperature
    REQUIRE(page.cpuGraph()->seriesCount() == 2);
}

TEST_CASE("tiles request the performance page", "[gui][summary]") {
    SummaryPage page;
    std::optional<Page> requested;
    QObject::connect(&page, &SummaryPage::pageRequested, &page, [&](Page p) { requested = p; });
    page.tiles().at(0)->click();
    REQUIRE(requested == Page::Performance);
}

TEST_CASE("no tile for absent hardware", "[gui][summary]") {
    SummaryPage page;
    REQUIRE(page.tiles().size() == 2);
}

TEST_CASE("an unknown logical cpu count reads as a dash, not zero", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    feed(page, h, Frame{});
    bool found = false;
    for (const auto* label : page.findChildren<QLabel*>()) {
        if (label->text().contains("logical processors")) found = true;
        REQUIRE_FALSE(label->text().startsWith("0 logical"));
    }
    REQUIRE_FALSE(found);  // replaced by a dash
}

TEST_CASE("hardware tiles appear only when present", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    feed(page, h, populatedFrame());
    REQUIRE(page.tiles().size() == 2);
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    REQUIRE(page.tiles().size() == 5);  // Disks, Network, Energy, GPU 0, Thermals
    feed(page, h, populatedFrame());
    REQUIRE(page.tiles().size() == 2);
}

TEST_CASE("a hardware tile opens its own panel", "[gui][summary]") {
    SummaryPage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    feed(page, h, f);
    std::optional<vm::Subsystem> asked;
    QObject::connect(&page, &SummaryPage::subsystemRequested, &page, [&](vm::Subsystem s) { asked = s; });
    page.tiles().at(2)->click();  // Energy
    REQUIRE(asked == vm::Subsystem::Energy);
    page.tiles().at(4)->click();  // Thermals
    REQUIRE(asked == vm::Subsystem::Thermals);
}

TEST_CASE("the tile of the second GPU asks for the second GPU", "[gui][summary][minor]") {
    SummaryPage page;
    HistoryStore h;
    auto f = populatedFrame();
    withHardware(f);
    f.gpus.push_back(f.gpus.front());
    f.gpus.back().index = 1;
    feed(page, h, f);
    std::optional<std::pair<vm::Subsystem, int>> asked;
    QObject::connect(&page, &SummaryPage::subsystemRequested, &page, [&](vm::Subsystem s, int d) { asked = {s, d}; });
    for (auto* tile : page.tiles()) {
        if (tile->title() == "GPU 1") tile->click();
    }
    REQUIRE(asked.has_value());
    REQUIRE(asked->first == vm::Subsystem::Gpu);
    REQUIRE(asked->second == 1);
}

TEST_CASE("a process-name tooltip is escaped, not interpreted", "[gui][summary][minor]") {
    SummaryPage page;
    HistoryStore h;
    page.resize(1000, 640);
    auto f = populatedFrame(8, 3);
    f.processes.processes[0].name = "<b>big</b>";
    f.processes.processes[0].cpu = Metric::measured(99, Unit::Percent);
    feed(page, h, f);
    bool seen = false;
    for (int r = 0; r < page.processTable()->rowCount(); ++r) {
        const auto tip = page.processTable()->item(r, 1)->toolTip();
        if (tip.contains("big")) {
            seen = true;
            REQUIRE_FALSE(tip.contains("<b>"));
            REQUIRE(tip.contains("&lt;b&gt;"));
        }
    }
    REQUIRE(seen);
}
