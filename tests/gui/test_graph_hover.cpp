// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/vm/graph_geometry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QMouseEvent>

#include <chrono>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

const MonoTime kNow = MonoTime{} + 1000s;

struct Rig {
    GraphCursorHub hub;
    HistoryRing<double> cpu{100}, mem{100};
    HistoryGraph a, b;
    Rig() {
        for (int i = 0; i < 61; ++i) {
            cpu.push(kNow - std::chrono::seconds(60 - i), 10.0 + i);
            mem.push(kNow - std::chrono::seconds(60 - i), 1024.0 * 1024 * (100 + i));
        }
        cpu.pushGap(kNow - 30s + 500ms);  // a gap slot between two samples
        a.resize(606, 120);
        b.resize(606, 120);
        a.setWindow(60000ms);
        b.setWindow(60000ms);
        a.setNow(kNow);
        b.setNow(kNow);
        a.addSeries(&cpu, vm::darkTheme().cpu, "CPU", Unit::Percent);
        b.addSeries(&mem, vm::darkTheme().memory, "Memory", Unit::Bytes);
        a.setCursorHub(&hub);
        b.setCursorHub(&hub);
    }
    void move(HistoryGraph& g, double x, double y = 20) {
        QMouseEvent e(QEvent::MouseMove, QPointF(x, y), g.mapToGlobal(QPointF(x, y)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&g, &e);
    }
};

}  // namespace

TEST_CASE("hovering sets the cursor time from x", "[gui][hover]") {
    Rig rig;
    rig.move(rig.a, 1 + 300);  // the middle of a 604 px plot starting at x = 1
    const auto t = rig.a.cursorTime();
    REQUIRE(t.has_value());
    const auto plot = rig.a.plotRect();
    REQUIRE(*t == *vm::timeAtX(300, {plot.width(), plot.height()}, kNow, 60000ms));
}

TEST_CASE("hover text shows the timestamp and the value, a dash at a gap", "[gui][hover]") {
    Rig rig;
    const double xAtAge10 = 1 + *vm::xAtTime(kNow - 10s, {rig.a.plotRect().width(), rig.a.plotRect().height()}, kNow, 60000ms);
    rig.move(rig.a, xAtAge10);
    const auto text = rig.a.hoverText();
    const auto lines = text.split('\n');
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].size() == 8);  // HH:MM:SS
    REQUIRE(lines[0][2] == ':');
    REQUIRE(lines[0][5] == ':');
    REQUIRE(lines[1] == "CPU  60.0%");  // age 10 s is sample 50: 10 + 50
    // the gap slot at age 29.5 s: nothing is measured there, so the tooltip says so
    const double xGap = 1 + *vm::xAtTime(kNow - 30s + 500ms, {rig.a.plotRect().width(), rig.a.plotRect().height()}, kNow, 60000ms);
    rig.move(rig.a, xGap);
    REQUIRE(rig.a.hoverText().split('\n')[1] == "CPU  —");
}

TEST_CASE("hover text names each series with its own unit", "[gui][hover]") {
    Rig rig;
    rig.move(rig.b, 300);
    const auto lines = rig.b.hoverText().split('\n');
    REQUIRE(lines[1].startsWith("Memory  "));
    REQUIRE(lines[1].endsWith("MB"));
}

TEST_CASE("linked graphs share one cursor", "[gui][hover]") {
    Rig rig;
    rig.move(rig.a, 200);
    REQUIRE(rig.b.cursorTime().has_value());
    REQUIRE(rig.b.cursorTime() == rig.a.cursorTime());
    REQUIRE_FALSE(rig.b.hoverText().isEmpty());
}

TEST_CASE("leaving clears the cursor in all linked graphs", "[gui][hover]") {
    Rig rig;
    rig.move(rig.a, 200);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&rig.a, &leave);
    REQUIRE_FALSE(rig.a.cursorTime().has_value());
    REQUIRE_FALSE(rig.b.cursorTime().has_value());
    REQUIRE(rig.b.hoverText().isEmpty());
}

TEST_CASE("moving outside the plot clears the cursor", "[gui][hover]") {
    Rig rig;
    rig.move(rig.a, 200);
    rig.move(rig.a, 200, 500);  // below the graph
    REQUIRE_FALSE(rig.a.cursorTime().has_value());
}

TEST_CASE("a still pointer does not repaint", "[gui][hover]") {
    Rig rig;
    rig.a.show();
    QCoreApplication::processEvents();
    rig.move(rig.a, 200);
    QCoreApplication::processEvents();
    const int painted = rig.a.paintCount();
    rig.move(rig.a, 200);  // the same time again: nothing changed
    rig.move(rig.a, 200);
    QCoreApplication::processEvents();
    REQUIRE(rig.a.paintCount() == painted);
    rig.move(rig.a, 260);  // a new time: one repaint
    QCoreApplication::processEvents();
    REQUIRE(rig.a.paintCount() == painted + 1);
}

TEST_CASE("the cursor stays on the same absolute time while the graph scrolls", "[gui][hover]") {
    Rig rig;
    rig.move(rig.a, 400);
    const auto t = rig.a.cursorTime();
    const auto plot = rig.a.plotRect();
    const double before = *vm::xAtTime(*t, {plot.width(), plot.height()}, kNow, 60000ms);
    rig.a.setNow(kNow + 5s);
    const double after = *vm::xAtTime(*t, {plot.width(), plot.height()}, kNow + 5s, 60000ms);
    REQUIRE(rig.a.cursorTime() == t);
    REQUIRE(after < before);  // the moment moved left with the data
}

TEST_CASE("graphs without a hub ignore the mouse", "[gui][hover]") {
    HistoryGraph g;
    g.resize(606, 120);
    QMouseEvent e(QEvent::MouseMove, QPointF(100, 20), QPointF(100, 20), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&g, &e);
    REQUIRE_FALSE(g.cursorTime().has_value());
    REQUIRE(g.hoverText().isEmpty());
}
