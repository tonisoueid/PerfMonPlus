// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/culprits_popover.hpp>
#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/processes_page.hpp>
#include <perfmonplus/gui/summary_page.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <chrono>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

const MonoTime kNow = MonoTime{} + 1000s;

ProcessRecord proc(int pid, std::string name, double cpu, double mem = 1000) {
    ProcessRecord r;
    r.key = {pid, static_cast<std::uint64_t>(pid) * 10};
    r.name = std::move(name);
    r.user = "dave";
    r.cpu = Metric::measured(cpu, Unit::Percent);
    r.memFootprint = Metric::measured(mem, Unit::Bytes);
    r.memPrivate = Metric::measured(mem / 2, Unit::Bytes);
    r.threads = Metric::measured(1, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
    r.gpu = Metric::measured(0, Unit::Percent);
    return r;
}

Frame frameAt(MonoTime t, std::vector<ProcessRecord> ps) {
    Frame f;
    f.mono = t;
    f.cpu.logical.resize(4);
    f.processes.processes = std::move(ps);
    return f;
}

void press(QWidget* w, double x, double y = 20) {
    QMouseEvent e(QEvent::MouseButtonPress, QPointF(x, y), w->mapToGlobal(QPointF(x, y)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}
void drag(QWidget* w, double x, double y = 20) {
    QMouseEvent e(QEvent::MouseMove, QPointF(x, y), w->mapToGlobal(QPointF(x, y)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}
void release(QWidget* w, double x, double y = 20) {
    QMouseEvent e(QEvent::MouseButtonRelease, QPointF(x, y), w->mapToGlobal(QPointF(x, y)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

struct Rig {
    HistoryRing<double> ring{100};
    HistoryGraph graph;
    std::vector<std::pair<MonoTime, MonoTime>> selected;
    Rig() {
        for (int i = 0; i < 61; ++i) ring.push(kNow - std::chrono::seconds(60 - i), i);
        graph.resize(606, 120);
        graph.setWindow(60000ms);
        graph.setNow(kNow);
        graph.addSeries(&ring, vm::darkTheme().cpu);
        graph.setSelectable(true);
        QObject::connect(&graph, &HistoryGraph::rangeSelected, &graph, [this](MonoTime a, MonoTime b, QPoint) { selected.push_back({a, b}); });
    }
};

}  // namespace

TEST_CASE("dragging selects a time range and emits it ordered", "[gui][culprits]") {
    Rig rig;
    press(&rig.graph, 400);
    drag(&rig.graph, 300);
    drag(&rig.graph, 200);  // dragged right to left
    release(&rig.graph, 200);
    REQUIRE(rig.selected.size() == 1);
    REQUIRE(rig.selected[0].first < rig.selected[0].second);
    REQUIRE(rig.graph.selection().has_value());
    REQUIRE(rig.graph.selection()->first == rig.selected[0].first);
}

TEST_CASE("a click without a drag clears the selection, and so does Escape", "[gui][culprits]") {
    Rig rig;
    press(&rig.graph, 400);
    drag(&rig.graph, 200);
    release(&rig.graph, 200);
    REQUIRE(rig.graph.selection().has_value());
    press(&rig.graph, 100);  // a plain click
    release(&rig.graph, 100);
    REQUIRE_FALSE(rig.graph.selection().has_value());
    REQUIRE(rig.selected.size() == 1);  // no second emission

    press(&rig.graph, 400);
    drag(&rig.graph, 200);
    release(&rig.graph, 200);
    REQUIRE(rig.graph.selection().has_value());
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&rig.graph, &esc);
    REQUIRE_FALSE(rig.graph.selection().has_value());
}

TEST_CASE("dragging past the edge sticks to it", "[gui][culprits]") {
    Rig rig;
    press(&rig.graph, 400);
    drag(&rig.graph, -500);
    release(&rig.graph, -500);
    REQUIRE(rig.selected.size() == 1);
    REQUIRE(rig.selected[0].first == kNow - 60s);  // the left edge of the window
}

TEST_CASE("the selection stays anchored to absolute time and disappears once outside the window", "[gui][culprits]") {
    Rig rig;
    press(&rig.graph, 400);
    drag(&rig.graph, 200);
    release(&rig.graph, 200);
    const auto before = *rig.graph.selection();
    rig.graph.setNow(kNow + 10s);  // sampling continues; the graph scrolls
    REQUIRE(rig.graph.selection().has_value());
    REQUIRE(rig.graph.selection()->first == before.first);  // the same moments
    rig.graph.setNow(kNow + 200s);
    REQUIRE_FALSE(rig.graph.selection().has_value());
}

TEST_CASE("a graph that is not selectable ignores drags", "[gui][culprits]") {
    Rig rig;
    rig.graph.setSelectable(false);
    press(&rig.graph, 400);
    drag(&rig.graph, 200);
    release(&rig.graph, 200);
    REQUIRE(rig.selected.empty());
    REQUIRE_FALSE(rig.graph.selection().has_value());
}

TEST_CASE("the popover lists processes by integrated cpu seconds including an exited one", "[gui][culprits]") {
    ProcessHistory history(60);
    for (int t = 0; t <= 3; ++t) {
        std::vector<ProcessRecord> ps{proc(1, "steady", 50)};
        if (t <= 2) ps.push_back(proc(2, "compiler", 100));
        history.ingest(frameAt(kNow - std::chrono::seconds(3 - t), ps), false);
    }
    QWidget parent;
    CulpritsPresenter presenter(&parent);
    presenter.setHistory(&history);
    presenter.setLogicalCpus(4);
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 3s, kNow, QPoint(0, 0));
    auto* table = presenter.popover()->table();
    REQUIRE(table->rowCount() == 2);
    REQUIRE(table->item(0, 0)->text() == "compiler");
    REQUIRE(table->item(0, 1)->text() == "8.0 CPU-seconds");
    REQUIRE(table->item(0, 2)->text().contains("exited"));
    REQUIRE(table->item(1, 0)->text() == "steady");
    REQUIRE(presenter.popover()->title().startsWith("Highest CPU use,"));
}

TEST_CASE("a memory selection ranks by peak", "[gui][culprits]") {
    ProcessHistory history(60);
    history.ingest(frameAt(kNow - 1s, {proc(1, "small", 1, 100), proc(2, "big", 1, 5000)}), false);
    history.ingest(frameAt(kNow, {proc(1, "small", 1, 120), proc(2, "big", 1, 6000)}), false);
    QWidget parent;
    CulpritsPresenter presenter(&parent);
    presenter.setHistory(&history);
    presenter.show(ProcessHistory::Resource::Memory, kNow - 2s, kNow, QPoint(0, 0));
    REQUIRE(presenter.popover()->table()->item(0, 0)->text() == "big");
    REQUIRE(presenter.popover()->title().startsWith("Highest memory use,"));
}

TEST_CASE("the popover is a snapshot", "[gui][culprits]") {
    ProcessHistory history(60);
    history.ingest(frameAt(kNow - 1s, {proc(1, "a", 10)}), false);
    history.ingest(frameAt(kNow, {proc(1, "a", 10)}), false);
    QWidget parent;
    CulpritsPresenter presenter(&parent);
    presenter.setHistory(&history);
    presenter.setLogicalCpus(1);
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 2s, kNow, QPoint(0, 0));
    const auto first = presenter.popover()->table()->item(0, 1)->text();
    history.ingest(frameAt(kNow + 1s, {proc(1, "a", 100), proc(9, "new", 100)}), false);  // sampling carries on
    REQUIRE(presenter.popover()->table()->rowCount() == 1);
    REQUIRE(presenter.popover()->table()->item(0, 1)->text() == first);
}

TEST_CASE("no history means a short explanation, not an empty popover", "[gui][culprits]") {
    QWidget parent;
    CulpritsPresenter presenter(&parent);  // no history set: per-process history is off
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 2s, kNow, QPoint(0, 0));
    REQUIRE(presenter.popover()->message().contains("Per-process history is off"));
    ProcessHistory empty(60);
    presenter.setHistory(&empty);
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 2s, kNow, QPoint(0, 0));
    REQUIRE(presenter.popover()->message().contains("No process data"));
}

TEST_CASE("Go to process selects the row in the Processes page", "[gui][culprits]") {
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    Frame f = frameAt(kNow, {proc(5, "culprit", 40)});
    f.generation = 1;
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(10000, Unit::Bytes);
    w.applyFrame({std::make_shared<const Frame>(f), false});
    w.goToProcess(ProcessKey{5, 50});
    REQUIRE(w.currentPage() == Page::Processes);
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page->selectedKey() == ProcessKey{5, 50});
}

TEST_CASE("an exited process that is no longer listed says so", "[gui][culprits]") {
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    Frame f = frameAt(kNow, {proc(5, "other", 1)});
    f.generation = 1;
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(10000, Unit::Bytes);
    w.applyFrame({std::make_shared<const Frame>(f), false});
    w.goToProcess(ProcessKey{999, 1});
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page->messageText().contains("has exited"));
    REQUIRE_FALSE(page->selectedKey().has_value());
}

TEST_CASE("the popover button emits the process it names", "[gui][culprits]") {
    ProcessHistory history(60);
    history.ingest(frameAt(kNow - 1s, {proc(7, "target", 10)}), false);
    history.ingest(frameAt(kNow, {proc(7, "target", 10)}), false);
    QWidget parent;
    CulpritsPresenter presenter(&parent);
    presenter.setHistory(&history);
    presenter.setLogicalCpus(1);
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 2s, kNow, QPoint(0, 0));
    std::optional<ProcessKey> got;
    QObject::connect(&presenter, &CulpritsPresenter::processRequested, &presenter, [&](ProcessKey k) { got = k; });
    presenter.popover()->goTo(0);
    REQUIRE(got == ProcessKey{7, 70});
    REQUIRE_FALSE(presenter.popover()->isVisible());
}

TEST_CASE("a range older than the history says how far back it reaches", "[gui][culprits]") {
    ProcessHistory history(3);
    for (int t = 0; t < 5; ++t) history.ingest(frameAt(kNow - std::chrono::seconds(4 - t), {proc(1, "a", 10)}), false);
    QWidget parent;
    CulpritsPresenter presenter(&parent);
    presenter.setHistory(&history);
    presenter.setLogicalCpus(1);
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 30s, kNow, QPoint(0, 0));  // far older than 3 samples
    REQUIRE(presenter.popover()->title().contains("history only reaches back to"));
    presenter.show(ProcessHistory::Resource::Cpu, kNow - 1s, kNow, QPoint(0, 0));    // fully covered
    REQUIRE_FALSE(presenter.popover()->title().contains("history only reaches back"));
}
