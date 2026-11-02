// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("main window has the product title and minimum size", "[gui]") {
    pmp::gui::MainWindow w;
    REQUIRE(w.windowTitle() == "PerfMonPlus");
    REQUIRE(w.minimumSize() == QSize(900, 575));
}
