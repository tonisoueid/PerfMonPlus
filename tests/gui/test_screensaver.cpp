// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/screensaver.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QShortcut>

using namespace pmp;
using namespace pmp::gui;

namespace {
void press(QWidget* w, Qt::Key key) {
    QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}
}  // namespace

TEST_CASE("F12 opens and Escape closes", "[gui][screensaver]") {
    MainWindow w;
    w.show();
    auto* saver = w.findChild<Screensaver*>();
    REQUIRE(saver);
    REQUIRE_FALSE(saver->isVisible());
    int closed = 0;
    QObject::connect(saver, &Screensaver::closed, saver, [&] { ++closed; });

    bool fired = false;
    for (auto* sc : w.findChildren<QShortcut*>()) {
        if (sc->key() == QKeySequence(Qt::Key_F12)) {
            emit sc->activated();
            fired = true;
        }
    }
    REQUIRE(fired);
    REQUIRE(saver->isVisible());
    REQUIRE(w.animationActive());  // the clock runs while the tunnel is showing

    press(saver, Qt::Key_Escape);
    REQUIRE_FALSE(saver->isVisible());
    REQUIRE(closed == 1);
}

TEST_CASE("any key closes the screensaver", "[gui][screensaver]") {
    Screensaver saver;
    saver.show();
    int closed = 0;
    QObject::connect(&saver, &Screensaver::closed, &saver, [&] { ++closed; });
    press(&saver, Qt::Key_A);
    REQUIRE_FALSE(saver.isVisible());
    REQUIRE(closed == 1);
}

TEST_CASE("the screensaver paints from the latest frame", "[gui][screensaver]") {
    Screensaver saver;
    saver.resize(320, 200);
    Frame f = pmp::testing::populatedFrame(4, 3);
    saver.setFrame(f);
    saver.tick(0.5);
    const QImage img = saver.grab().toImage();
    REQUIRE(img.width() == 320);
    bool nonBackground = false;
    for (int y = 0; y < img.height() && !nonBackground; ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (img.pixel(x, y) != img.pixel(0, 0)) {
                nonBackground = true;
                break;
            }
        }
    }
    REQUIRE(nonBackground);
}
