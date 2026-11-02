// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/colors_popover.hpp>
#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/summary_page.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::testing;

namespace {

FrameUpdate update(Frame f) { return {std::make_shared<const Frame>(std::move(f)), false}; }

QRgb backgroundPixel(MainWindow& w) { return w.grab().toImage().convertToFormat(QImage::Format_RGB32).pixel(900, 8); }

QRgb firstSegmentPixel(MainWindow& w) {
    auto* page = dynamic_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    auto* bar = page->cpuMeter();
    bar->settleForTest();
    const auto img = bar->grab().toImage().convertToFormat(QImage::Format_RGB32);
    return img.pixel(bar->segmentRect(0).center().toPoint());
}

MainWindow& prepared(MainWindow& w) {
    w.resize(1200, 760);
    w.show();
    QApplication::processEvents();
    auto f = populatedFrame();
    f.cpu.total = Metric::measured(100, Unit::Percent);
    w.applyFrame(update(f));
    QApplication::processEvents();
    return w;
}

AppSettings with(std::string theme, int saturation, bool bloom = true) {
    AppSettings s;
    s.themeName = std::move(theme);
    s.saturation = saturation;
    s.bloom = bloom;
    return s;
}

bool grey(QRgb p) { return std::abs(qRed(p) - qGreen(p)) <= 2 && std::abs(qGreen(p) - qBlue(p)) <= 2; }

}  // namespace

TEST_CASE("choosing Light changes the window background", "[gui][colors]") {
    MainWindow w;
    prepared(w);
    const QRgb dark = backgroundPixel(w);
    w.applySettings(with("light", 7));
    const QRgb light = backgroundPixel(w);
    REQUIRE(dark != light);
    const auto expected = vm::lightTheme().background;
    REQUIRE(qRed(light) == expected.r);
    REQUIRE(qGreen(light) == expected.g);
    REQUIRE(qBlue(light) == expected.b);
}

TEST_CASE("saturation 0 greys the cpu meter", "[gui][colors]") {
    MainWindow w;
    prepared(w);
    w.applySettings(with("dark", 7));
    REQUIRE_FALSE(grey(firstSegmentPixel(w)));
    w.applySettings(with("dark", 0));
    REQUIRE(grey(firstSegmentPixel(w)));
}

TEST_CASE("phosphor theme ignores saturation", "[gui][colors]") {
    MainWindow w;
    prepared(w);
    w.applySettings(with("green", 0));
    const QRgb a = firstSegmentPixel(w);
    w.applySettings(with("green", 11));
    REQUIRE(firstSegmentPixel(w) == a);
}

TEST_CASE("settings changes are reported to the app", "[gui][colors]") {
    MainWindow w;
    std::optional<AppSettings> seen;
    QObject::connect(&w, &MainWindow::settingsChanged, &w, [&](AppSettings s) { seen = s; });
    w.applySettings(with("green", 3, false));
    REQUIRE(seen);
    REQUIRE(seen->themeName == "green");
    REQUIRE(seen->saturation == 3);
    REQUIRE_FALSE(seen->bloom);
}

TEST_CASE("bloom toggle persists", "[gui][colors]") {
    ColorsPopover pop(AppSettings{});
    std::optional<AppSettings> seen;
    QObject::connect(&pop, &ColorsPopover::settingsChanged, &pop, [&](AppSettings s) { seen = s; });
    pop.bloomCheck()->setChecked(false);
    REQUIRE(seen);
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    seen->save(store);
    store.sync();
    REQUIRE_FALSE(AppSettings::load(store).bloom);
}

TEST_CASE("low effects forces bloom off and caps the frame rate", "[gui][colors]") {
    ColorsPopover pop(AppSettings{});
    std::optional<AppSettings> seen;
    QObject::connect(&pop, &ColorsPopover::settingsChanged, &pop, [&](AppSettings s) { seen = s; });
    pop.lowEffectsCheck()->setChecked(true);
    REQUIRE(seen);
    REQUIRE_FALSE(seen->bloom);
    REQUIRE(seen->frameRate <= 30);
    REQUIRE(seen->lowEffects);
}

TEST_CASE("popover controls emit theme and saturation", "[gui][colors]") {
    ColorsPopover pop(AppSettings{});
    std::optional<AppSettings> seen;
    QObject::connect(&pop, &ColorsPopover::settingsChanged, &pop, [&](AppSettings s) { seen = s; });
    pop.themeButton("green")->click();
    REQUIRE(seen->themeName == "green");
    pop.saturationSlider()->setValue(4);
    REQUIRE(seen->saturation == 4);
    pop.frameRateCombo()->setCurrentIndex(pop.frameRateCombo()->findData(15));
    REQUIRE(seen->frameRate == 15);
}

TEST_CASE("an unknown saved theme falls back to dark", "[gui][colors]") {
    MainWindow w;
    prepared(w);
    w.applySettings(with("zzz", 7));
    const QRgb p = backgroundPixel(w);
    const auto d = vm::darkTheme().background;
    REQUIRE(qRed(p) == d.r);
    REQUIRE(qBlue(p) == d.b);
}

TEST_CASE("the colors popover paints the themed panel colour, never white, in every theme", "[gui][colors]") {
    MainWindow w;
    prepared(w);
    for (const char* name : {"dark", "light", "green"}) {
        w.applySettings(with(name, 7));
        ColorsPopover pop(with(name, 7), &w);
        pop.adjustSize();
        pop.show();
        QApplication::processEvents();
        const auto img = pop.grab().toImage().convertToFormat(QImage::Format_RGB32);
        const QRgb p = img.pixel(img.width() - 6, 6);
        const auto panel = vm::themeByName(name)->panel;
        INFO(name);
        REQUIRE(qRed(p) == panel.r);
        REQUIRE(qGreen(p) == panel.g);
        REQUIRE(qBlue(p) == panel.b);
        pop.hide();
    }
}
