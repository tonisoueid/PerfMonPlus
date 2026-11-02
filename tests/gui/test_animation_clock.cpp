// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/animation_clock.hpp>
#include <perfmonplus/gui/main_window.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <vector>

using namespace pmp::gui;

namespace {

std::vector<double> collect(AnimationClock& clock, int ms) {
    std::vector<double> dts;
    QEventLoop loop;
    QObject::connect(&clock, &AnimationClock::tick, &loop, [&](double dt) { dts.push_back(dt); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return dts;
}

}  // namespace

TEST_CASE("clock ticks at roughly the requested rate", "[gui][clock]") {
    AnimationClock clock;
    clock.setFrameRate(30);
    clock.setActive(true);
    const auto dts = collect(clock, 300);
    REQUIRE(dts.size() >= 6);
    REQUIRE(dts.size() <= 12);
    for (double dt : dts) {
        REQUIRE(dt > 0.02);
        REQUIRE(dt < 0.08);
    }
}

TEST_CASE("an inactive clock does not tick", "[gui][clock]") {
    AnimationClock clock;
    clock.setActive(false);
    REQUIRE(collect(clock, 200).empty());
    REQUIRE_FALSE(clock.isActive());
}

TEST_CASE("frame rate maps to a timer interval", "[gui][clock]") {
    AnimationClock clock;
    clock.setFrameRate(60);
    REQUIRE(clock.intervalMs() == 17);
    clock.setFrameRate(15);
    REQUIRE(clock.intervalMs() == 67);
    clock.setFrameRate(0);  // nonsense is clamped, never a divide by zero
    REQUIRE(clock.intervalMs() >= 1);
}

TEST_CASE("animation stops when hidden or minimised", "[gui][clock]") {
    MainWindow w;
    w.show();
    QApplication::processEvents();
    REQUIRE(w.animationActive());
    w.hide();
    QApplication::processEvents();
    REQUIRE_FALSE(w.animationActive());
    w.show();
    QApplication::processEvents();
    REQUIRE(w.animationActive());
    w.showMinimized();
    QApplication::processEvents();
    REQUIRE_FALSE(w.animationActive());
}

TEST_CASE("animation stops on pages without live visuals", "[gui][clock]") {
    MainWindow w;
    w.show();
    QApplication::processEvents();
    w.showPage(Page::Users);
    REQUIRE_FALSE(w.animationActive());
    w.showPage(Page::Performance);
    REQUIRE(w.animationActive());
}

TEST_CASE("the frame rate setting is applied", "[gui][clock]") {
    MainWindow w;
    AppSettings s;
    s.frameRate = 15;
    w.applySettings(s);
    REQUIRE(w.animationIntervalMs() >= 65);
    REQUIRE(w.animationIntervalMs() <= 68);
    s.lowEffects = true;
    s.frameRate = 120;
    w.applySettings(s);
    REQUIRE(w.animationIntervalMs() >= 33);  // low effects caps at 30 Hz
}
