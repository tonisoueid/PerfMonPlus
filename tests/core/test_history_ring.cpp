// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_ring.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

namespace {
pmp::MonoTime at(int ms) { return pmp::MonoTime{} + std::chrono::milliseconds(ms); }
}  // namespace

TEST_CASE("ring keeps only the newest capacity entries", "[ring]") {
    pmp::HistoryRing<double> r(3);
    for (int i = 1; i <= 4; ++i) r.push(at(i), i);
    REQUIRE(r.size() == 3);
    REQUIRE(r.at(0).value == 2.0);
    REQUIRE(r.at(2).value == 4.0);
}

TEST_CASE("ring stores gaps with their timestamp", "[ring]") {
    pmp::HistoryRing<double> r(4);
    r.push(at(1), 1.0);
    r.pushGap(at(2000));
    REQUIRE(r.size() == 2);
    REQUIRE_FALSE(r.at(1).value.has_value());
    REQUIRE(r.at(1).t == at(2000));
}

TEST_CASE("ring rejects zero capacity and out-of-range access", "[ring]") {
    REQUIRE_THROWS_AS(pmp::HistoryRing<double>(0), std::invalid_argument);
    pmp::HistoryRing<double> r(3);
    r.push(at(1), 1.0);
    r.push(at(2), 2.0);
    r.push(at(3), 3.0);
    REQUIRE_THROWS_AS(r.at(5), std::out_of_range);
}
