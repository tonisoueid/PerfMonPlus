// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version string matches project version", "[version]") {
    REQUIRE(pmp::versionString() == "0.1.0");
}
