// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/app_icon.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QPixmap>

TEST_CASE("the embedded application icon is not null and renders pixels [gui]") {
    const QIcon icon = pmp::gui::applicationIcon();
    REQUIRE_FALSE(icon.isNull());
    const QPixmap pm = icon.pixmap(64, 64);
    REQUIRE_FALSE(pm.isNull());
    REQUIRE(pm.toImage().convertToFormat(QImage::Format_ARGB32).pixelColor(32, 32).alpha() > 0);
}
