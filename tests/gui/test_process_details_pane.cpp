// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/process_details_pane.hpp>
#include <perfmonplus/gui/processes_page.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QTimer>

#include <unistd.h>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

MonoTime at(int s) { return MonoTime{} + std::chrono::seconds(s); }

std::string statLine(int start) {
    return "42 (x) S 1 42 42 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 " + std::to_string(start) + " 1000 10 0";
}

ProcessRecord record(int pid = 42, std::uint64_t start = 5000) {
    ProcessRecord r;
    r.key = {pid, start};
    r.name = "Firefox";
    r.user = "dave";
    r.state = 'S';
    r.cpu = Metric::measured(12.5, Unit::Percent);
    r.cpuTime = Metric::measured(60, Unit::Seconds);
    r.gpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
    r.memFootprint = Metric::measured(500.0 * 1024 * 1024, Unit::Bytes);
    r.memPrivate = Metric::measured(400.0 * 1024 * 1024, Unit::Bytes);
    r.pageFaults = Metric::measured(10, Unit::Count);
    r.threads = Metric::measured(4, Unit::Count);
    r.priority = Metric::measured(0, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
    r.exe = "/usr/lib/firefox/firefox";
    return r;
}

Frame frameWith(ProcessRecord r, int t = 100) {
    Frame f;
    f.mono = at(t);
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(8.0 * 1024 * 1024 * 1024, Unit::Bytes);
    f.processes.processes = {std::move(r)};
    return f;
}

bool waitInspected(ProcessDetailsPane& pane, int ms = 3000) {
    QEventLoop loop;
    bool fired = false;
    QObject::connect(&pane, &ProcessDetailsPane::inspected, &loop, [&] {
        fired = true;
        loop.quit();
    });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return fired;
}

struct Env {
    FakeRoot fr;
    Env() {
        fr.write("proc/42/stat", statLine(5000));
        fr.write("proc/42/cmdline", std::string("firefox\0-new-window\0", 20));
    }
    std::shared_ptr<const linux::ProcessInspector> inspector() { return std::make_shared<linux::ProcessInspector>(fr.root()); }
};

}  // namespace

TEST_CASE("the pane shows the five columns for the selected process", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    ProcessRecord parent = record(1, 1);
    parent.name = "gnome-shell";
    pane.setProcess(&r, &parent);
    pane.update(frameWith(r), nullptr);
    REQUIRE(pane.titleText() == "Firefox (42)");
    REQUIRE(pane.pathText() == "/usr/lib/firefox/firefox");
    REQUIRE(pane.valueOf("IDENTITY", "Parent") == "gnome-shell (1)");
    REQUIRE(pane.valueOf("IDENTITY", "User") == "dave");
    REQUIRE(pane.valueOf("LIFETIME", "Threads") == "4");
    REQUIRE(pane.valueOf("PROCESSOR", "CPU") == "12.5%");
    REQUIRE(pane.valueOf("MEMORY", "Footprint") == "500.0 MB");
    REQUIRE(pane.valueOf("DISK", "Reading") == "Zero KB/s");
    REQUIRE(waitInspected(pane));
}

TEST_CASE("the command line is hidden until Show is clicked", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    pane.setProcess(&r, nullptr);
    REQUIRE(waitInspected(pane));
    REQUIRE(pane.commandLineText().isEmpty());  // inspected, but not shown (INT-09)
    const int before = pane.inspectionCount();
    pane.showCommandLineButton()->click();
    REQUIRE(pane.commandLineText() == "firefox -new-window");
    REQUIRE(pane.inspectionCount() == before);  // revealed from what was already read
    pane.showCommandLineButton()->click();
    REQUIRE(pane.commandLineText().isEmpty());
    pane.showCommandLineButton()->click();
    pane.setProcess(nullptr, nullptr);
    auto other = record(43, 6000);
    pane.setProcess(&other, nullptr);
    REQUIRE(pane.commandLineText().isEmpty());  // a new selection starts hidden again
}

TEST_CASE("the environment is not read unless enabled", "[gui][details]") {
    if (geteuid() == 0) SKIP("root can read everything");
    Env env;
    env.fr.write("proc/42/environ", std::string("SECRET=1\0", 9));
    env.fr.makeUnreadable("proc/42/environ");
    ProcessDetailsPane pane(env.inspector());
    pane.show();
    auto r = record();
    pane.setProcess(&r, nullptr);
    REQUIRE(waitInspected(pane));
    REQUIRE(pane.environmentText().isEmpty());  // not requested: not even attempted
    pane.setShowEnvironment(true);
    REQUIRE(waitInspected(pane));
    REQUIRE(pane.environmentText() == "Restricted");
}

TEST_CASE("an exited process is explained and keeps its numbers", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    pane.setProcess(&r, nullptr);
    REQUIRE(waitInspected(pane));
    REQUIRE(pane.statusText().isEmpty());

    env.fr.write("proc/42/stat", statLine(9999));  // the PID now belongs to someone else
    ProcessDetailsPane later(env.inspector());     // a fresh selection of the same (now stale) key
    later.setProcess(&r, nullptr);
    later.update(frameWith(r), nullptr);
    REQUIRE(waitInspected(later));
    REQUIRE(later.statusText() == "This process has exited");
    REQUIRE(later.titleText() == "Firefox (42)");
    REQUIRE(later.valueOf("PROCESSOR", "CPU") == "12.5%");
}

TEST_CASE("a tombstone shows its exit note", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    r.exitedAt = at(99);
    pane.setProcess(&r, nullptr);
    pane.update(frameWith(r), nullptr);
    REQUIRE(pane.valueOf("IDENTITY", "Status") == "exited");
    REQUIRE(waitInspected(pane));
}

TEST_CASE("restricted fields show Restricted", "[gui][details]") {
    if (geteuid() == 0) SKIP("root can read everything");
    Env env;
    env.fr.makeUnreadable("proc/42/cmdline");
    env.fr.makeDir("proc/42/fd");
    env.fr.makeUnreadable("proc/42/fd");
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    r.exe.clear();
    r.exeRestricted = true;
    r.diskReadBps = Metric::notMeasured(Availability::Restricted, Unit::BytesPerSec);
    pane.setProcess(&r, nullptr);
    pane.update(frameWith(r), nullptr);
    REQUIRE(waitInspected(pane));
    REQUIRE(pane.pathText() == "Restricted");
    REQUIRE(pane.openFilesText() == "Restricted");
    REQUIRE(pane.valueOf("DISK", "Reading") == "Restricted");
    pane.showCommandLineButton()->click();
    REQUIRE(pane.commandLineText() == "Restricted");
}

TEST_CASE("labels are plain text", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    r.name = "<b>bold</b><a href=\"http://evil\">link</a>";
    r.exe = "/tmp/<i>x</i>";
    pane.setProcess(&r, nullptr);
    pane.update(frameWith(r), nullptr);
    REQUIRE(pane.titleText() == "<b>bold</b><a href=\"http://evil\">link</a> (42)");
    REQUIRE(pane.pathText() == "/tmp/<i>x</i>");
    for (auto* label : pane.findChildren<QLabel*>()) {
        REQUIRE(label->textFormat() == Qt::PlainText);
        REQUIRE_FALSE(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    }
    REQUIRE(waitInspected(pane));
}

TEST_CASE("the mini graphs follow the selected process", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    ProcessHistory history(20);
    auto r = record();
    for (int t = 0; t < 3; ++t) history.ingest(frameWith(r, 100 + t), false);
    pane.setProcess(&r, nullptr);
    pane.update(frameWith(r, 102), &history);
    REQUIRE(pane.cpuGraph()->seriesCount() == 1);
    REQUIRE(pane.memoryGraph()->seriesCount() == 1);
    pane.setProcess(nullptr, nullptr);
    REQUIRE(pane.cpuGraph()->seriesCount() == 0);
    REQUIRE(waitInspected(pane, 200) == false);  // an answer for the old selection is dropped
}

TEST_CASE("inspection is not repeated every frame", "[gui][details]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    pane.setProcess(&r, nullptr);
    for (int t = 0; t < 10; ++t) pane.update(frameWith(r, 100 + t), nullptr);
    REQUIRE(pane.inspectionCount() == 1);
    REQUIRE(waitInspected(pane));
}

TEST_CASE("Enter toggles the details pane and the choice is remembered", "[gui][details][processes]") {
    Env env;
    ProcessesPage page(env.inspector());
    page.resize(1000, 700);
    page.show();
    HistoryStore h;
    auto r = record();
    r.kind = ProcessKind::Background;
    const auto f = frameWith(r);
    page.update(f, h, f.mono);
    REQUIRE(page.selectProcess(r.key));
    REQUIRE(page.details()->isVisibleTo(&page));
    REQUIRE(page.detailsOpen());
    int changed = 0;
    QObject::connect(&page, &ProcessesPage::settingsChanged, &page, [&] { ++changed; });
    emit page.table()->activated(page.table()->currentIndex());  // what Enter does
    REQUIRE_FALSE(page.details()->isVisibleTo(&page));
    REQUIRE_FALSE(page.detailsOpen());
    REQUIRE(changed == 1);
    AppSettings s;
    page.exportSettings(s);
    REQUIRE_FALSE(s.detailsPaneOpen);
    emit page.table()->activated(page.table()->currentIndex());
    REQUIRE(page.details()->isVisibleTo(&page));
}

TEST_CASE("the pane is hidden until something is selected", "[gui][details][processes]") {
    Env env;
    ProcessesPage page(env.inspector());
    page.show();
    HistoryStore h;
    const auto f = frameWith(record());
    page.update(f, h, f.mono);
    REQUIRE_FALSE(page.details()->isVisibleTo(&page));
}

TEST_CASE("a hidden pane does not keep inspecting", "[gui][details][minor]") {
    Env env;
    ProcessDetailsPane pane(env.inspector());
    auto r = record();
    pane.show();
    pane.setProcess(&r, nullptr);
    REQUIRE(pane.timerActive());
    pane.hide();
    REQUIRE_FALSE(pane.timerActive());
    const int before = pane.inspectionCount();
    pane.show();
    REQUIRE(pane.timerActive());
    REQUIRE(pane.inspectionCount() == before + 1);  // refreshed on return: what was shown is stale
    ProcessDetailsPane hidden(env.inspector());
    hidden.setProcess(&r, nullptr);
    REQUIRE_FALSE(hidden.timerActive());  // selected while hidden: nothing ticks until it is shown
}

TEST_CASE("a double click does not toggle the details pane, Enter does", "[gui][details][processes][minor]") {
    Env env;
    ProcessesPage page(env.inspector());
    page.resize(1000, 700);
    page.show();
    HistoryStore h;
    auto r = record();
    r.kind = ProcessKind::Background;
    const auto f = frameWith(r);
    page.update(f, h, f.mono);
    REQUIRE(page.selectProcess(r.key));
    REQUIRE(page.detailsOpen());
    emit page.table()->doubleClicked(page.table()->currentIndex());
    emit page.table()->activated(page.table()->currentIndex());  // the second half of a double click
    REQUIRE(page.detailsOpen());
    QCoreApplication::processEvents();
    emit page.table()->activated(page.table()->currentIndex());  // Enter
    REQUIRE_FALSE(page.detailsOpen());
}
