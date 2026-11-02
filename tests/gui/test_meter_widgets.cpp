// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/meter_widgets.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QProcessEnvironment>

#include <cmath>
#include <cstdlib>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

bool near(QRgb px, vm::Rgb c, int tol = 6) {
    return std::abs(qRed(px) - c.r) <= tol && std::abs(qGreen(px) - c.g) <= tol && std::abs(qBlue(px) - c.b) <= tol;
}

bool imageContains(const QImage& img, vm::Rgb c, int tol = 6) {
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (near(img.pixel(x, y), c, tol)) return true;
    return false;
}

QImage shot(QWidget& w) { return w.grab().toImage().convertToFormat(QImage::Format_RGB32); }

MonoTime t(int sec) { return MonoTime{} + std::chrono::seconds(1000 + sec); }

}  // namespace

TEST_CASE("segment bar lights the right count", "[gui][widgets]") {
    SegmentBar bar(vm::MeterKind::Cpu, Qt::Horizontal, 10);
    bar.setTheme(vm::darkTheme());
    bar.setBloom(false);
    bar.resize(300, 30);
    bar.setMetric(Metric::measured(50, Unit::Percent), 100);
    bar.settleForTest();
    REQUIRE(bar.litCount() == 5);

    const auto img = shot(bar);
    const auto lit = bar.segmentRect(0).center().toPoint();
    const auto unlit = bar.segmentRect(9).center().toPoint();
    REQUIRE(near(img.pixel(lit), vm::segmentColor(vm::darkTheme(), vm::MeterKind::Cpu, 0, 10)));
    REQUIRE_FALSE(near(img.pixel(unlit), vm::segmentColor(vm::darkTheme(), vm::MeterKind::Cpu, 9, 10)));
}

TEST_CASE("unavailable metric leaves the bar dark", "[gui][widgets]") {
    SegmentBar bar(vm::MeterKind::Cpu, Qt::Horizontal, 10);
    bar.setTheme(vm::darkTheme());
    bar.resize(300, 30);
    bar.setMetric(Metric::notMeasured(Availability::Pending, Unit::Percent), 100);
    bar.settleForTest();
    REQUIRE(bar.litCount() == 0);
    REQUIRE_FALSE(imageContains(shot(bar), vm::darkTheme().cpu));
}

TEST_CASE("bloom adds glow around lit segments", "[gui][widgets]") {
    const auto theme = vm::darkTheme();
    auto outsidePixel = [&](bool bloom) {
        SegmentBar bar(vm::MeterKind::Memory, Qt::Horizontal, 10);
        bar.setTheme(theme);
        bar.setBloom(bloom);
        bar.resize(300, 30);
        bar.setMetric(Metric::measured(100, Unit::Percent), 100);
        bar.settleForTest();
        const auto r = bar.segmentRect(0);
        return shot(bar).pixel(QPoint(static_cast<int>(r.left()) - 2, static_cast<int>(r.center().y())));
    };
    REQUIRE(near(outsidePixel(false), theme.panel, 2));
    REQUIRE_FALSE(near(outsidePixel(true), theme.panel, 2));
}

TEST_CASE("graph draws the line and breaks at gaps", "[gui][widgets]") {
    const vm::Rgb magenta{255, 0, 255};
    HistoryRing<double> ring(20);
    ring.push(t(-30), 50);
    ring.push(t(-20), 50);
    ring.pushGap(t(-10));
    ring.push(t(-5), 50);
    ring.push(t(0), 50);

    HistoryGraph g;
    g.setTheme(vm::darkTheme());
    g.setBloom(false);
    g.resize(620, 120);
    g.addSeries(&ring, magenta);
    g.setFixedScale({0, 100});
    g.setWindow(60s);
    g.setNow(t(0));

    const auto img = shot(g);
    const auto plot = g.plotRect();
    const auto colHasSeries = [&](double plotX) {
        const int x = static_cast<int>(plot.left() + plotX * plot.width() / 600.0);
        for (int y = 0; y < img.height(); ++y)
            if (near(img.pixel(x, y), magenta, 40)) return true;
        return false;
    };
    REQUIRE(colHasSeries(350));  // between t(-30)=300 and t(-20)=400
    REQUIRE(colHasSeries(575));  // between t(-5)=550 and t(0)=600
    REQUIRE_FALSE(colHasSeries(470));  // inside the gap
    REQUIRE_FALSE(colHasSeries(520));
}

TEST_CASE("graph with only gaps draws no line", "[gui][widgets]") {
    const vm::Rgb magenta{255, 0, 255};
    HistoryRing<double> ring(20);
    ring.pushGap(t(-5));
    ring.pushGap(t(0));
    HistoryGraph g;
    g.setTheme(vm::darkTheme());
    g.setBloom(false);
    g.resize(620, 120);
    g.addSeries(&ring, magenta);
    g.setFixedScale({0, 100});
    g.setNow(t(0));
    REQUIRE_FALSE(imageContains(shot(g), magenta, 60));
}

TEST_CASE("a gap in the data is marked, not left looking like a short dip", "[gui][widgets][minor]") {
    HistoryRing<double> ring(20);
    for (int s = -9; s <= -4; ++s) ring.push(t(s), 50.0);
    ring.pushGap(t(-3));
    for (int s = -2; s <= 0; ++s) ring.push(t(s), 50.0);
    HistoryGraph g;
    g.setTheme(vm::darkTheme());
    g.setBloom(false);
    g.resize(620, 120);
    g.addSeries(&ring, vm::Rgb{255, 0, 255});
    g.setFixedScale({0, 100});
    g.setWindow(std::chrono::seconds(10));
    g.setNow(t(0));
    const QImage img = shot(g);
    const int y = img.height() / 10;                 // above the line and between grid rows
    const int gapX = static_cast<int>(img.width() * 0.7);
    const int emptyX = 12;                            // before the first sample: plain panel
    REQUIRE(img.pixel(gapX, y) != img.pixel(emptyX, y));
}

TEST_CASE("stat block shows its caption and value", "[gui][widgets]") {
    StatBlock b("Utilization");
    b.setValue("9.8%");
    REQUIRE(b.value() == "9.8%");
    REQUIRE(b.sizeHint().isValid());
    REQUIRE(b.sizeHint().width() > 0);
}

TEST_CASE("widgets render at high DPI without crashing", "[gui][widgets]") {
    HistoryGraph g;
    g.setTheme(vm::darkTheme());
    g.resize(200, 100);
    QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(2);
    img.fill(Qt::transparent);
    g.render(&img);
    bool painted = false;
    for (int y = 0; y < img.height() && !painted; ++y)
        for (int x = 0; x < img.width() && !painted; ++x) painted = qAlpha(img.pixel(x, y)) != 0;
    REQUIRE(painted);
}

// Hidden by default; run with `[.visual]` and PMP_VISUAL_DIR=<dir> to write a contact sheet for eyeballing.
TEST_CASE("visual: widget sheet", "[.visual]") {
    const auto dir = qEnvironmentVariable("PMP_VISUAL_DIR");
    REQUIRE_FALSE(dir.isEmpty());
    QWidget sheet;
    sheet.resize(640, 260);
    const auto theme = vm::darkTheme();
    SegmentBar a(vm::MeterKind::Cpu, Qt::Horizontal, 30, &sheet);
    a.setTheme(theme);
    a.setGeometry(10, 10, 620, 26);
    a.setMetric(Metric::measured(62, Unit::Percent), 100);
    a.settleForTest();
    SegmentBar b(vm::MeterKind::Memory, Qt::Horizontal, 30, &sheet);
    b.setTheme(theme);
    b.setGeometry(10, 44, 620, 26);
    b.setMetric(Metric::measured(35, Unit::Percent), 100);
    b.settleForTest();
    HistoryRing<double> ring(100);
    for (int i = -50; i <= 0; ++i) ring.push(t(i), 50 + 40 * std::sin(i / 6.0));
    HistoryGraph g(&sheet);
    g.setTheme(theme);
    g.setGeometry(10, 80, 620, 170);
    g.addSeries(&ring, theme.cpu);
    g.setFixedScale({0, 100});
    g.setNow(t(0));
    g.setCaption("Scrolling history");
    sheet.grab().save(dir + "/widgets.png");
}

TEST_CASE("graph repaints only when the scroll moves a visible amount", "[gui][widgets]") {
    HistoryGraph g;
    g.setTheme(vm::darkTheme());
    g.resize(620, 120);
    g.setWindow(60s);
    g.show();
    g.setNow(t(0));
    QApplication::processEvents();
    const int base = g.paintCount();
    REQUIRE(base >= 1);

    // 60 s across ~600 px is ~10 px/s: ten milliseconds is a tenth of a pixel, so nothing to redraw.
    for (int i = 1; i <= 10; ++i) g.setNow(t(0) + std::chrono::milliseconds(i));
    QApplication::processEvents();
    REQUIRE(g.paintCount() == base);

    g.setNow(t(1));  // a full second is ~10 px: must repaint
    QApplication::processEvents();
    REQUIRE(g.paintCount() == base + 1);
}

TEST_CASE("segment bar repaints only when the displayed segment count changes", "[gui][widgets]") {
    SegmentBar bar(vm::MeterKind::Cpu, Qt::Horizontal, 10);
    bar.setTheme(vm::darkTheme());
    bar.resize(300, 30);
    bar.show();
    bar.setMetric(Metric::measured(50, Unit::Percent), 100);
    bar.settleForTest();
    QApplication::processEvents();
    const int base = bar.paintCount();
    REQUIRE(bar.litCount() == 5);

    bar.setMetric(Metric::measured(60, Unit::Percent), 100);  // new target: nothing visible has changed yet
    QApplication::processEvents();
    REQUIRE(bar.paintCount() == base);

    bar.tick(0.001);  // eases a hair: still five segments
    QApplication::processEvents();
    REQUIRE(bar.litCount() == 5);
    REQUIRE(bar.paintCount() == base);

    bar.tick(5.0);  // arrives: six segments
    QApplication::processEvents();
    REQUIRE(bar.litCount() == 6);
    REQUIRE(bar.paintCount() == base + 1);
}

TEST_CASE("a bar goes dark at once when its reading stops being available", "[gui][widgets]") {
    SegmentBar bar(vm::MeterKind::Cpu, Qt::Horizontal, 10);
    bar.setTheme(vm::darkTheme());
    bar.resize(300, 30);
    bar.setMetric(Metric::measured(80, Unit::Percent), 100);
    bar.settleForTest();
    REQUIRE(bar.litCount() == 8);
    bar.setMetric(Metric::notMeasured(Availability::Failed, Unit::Percent), 100);
    REQUIRE(bar.litCount() == 0);  // no easing down: an unavailable reading must not look like a value
}
