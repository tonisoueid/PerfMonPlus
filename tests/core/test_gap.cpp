// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/gap.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("on-time sample is not a gap", "[gap]") {
    REQUIRE_FALSE(pmp::shouldInsertGap(1s, 1000ms, 1000ms));
}
TEST_CASE("slightly late sample is not a gap", "[gap]") {
    REQUIRE_FALSE(pmp::shouldInsertGap(1s, 1900ms, 1900ms));
}
TEST_CASE("sample later than 2x interval is a gap", "[gap]") {
    REQUIRE(pmp::shouldInsertGap(1s, 2500ms, 2500ms));
}
TEST_CASE("suspend is a gap", "[gap]") {
    REQUIRE(pmp::shouldInsertGap(1s, 1000ms, 61s));
}
TEST_CASE("small boot/mono drift is not a gap", "[gap]") {
    REQUIRE_FALSE(pmp::shouldInsertGap(1s, 1000ms, 1300ms));
}
