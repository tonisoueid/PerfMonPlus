// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/meter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using Catch::Approx;

namespace {
Metric pct(double v) { return Metric::measured(v, Unit::Percent); }
}  // namespace

TEST_CASE("unavailable lights nothing", "[meter]") {
    REQUIRE(litSegments(Metric::notMeasured(Availability::Pending, Unit::Percent), 100, 30) == 0);
    REQUIRE(litSegments(Metric::notMeasured(Availability::Unsupported, Unit::Percent), 100, 30) == 0);
}

TEST_CASE("measured zero lights nothing, tiny nonzero lights one", "[meter]") {
    REQUIRE(litSegments(pct(0.0), 100, 30) == 0);
    REQUIRE(litSegments(pct(0.1), 100, 30) == 1);
}

TEST_CASE("full scale lights all and overflow clamps", "[meter]") {
    REQUIRE(litSegments(pct(100), 100, 30) == 30);
    REQUIRE(litSegments(pct(250), 100, 30) == 30);
    REQUIRE(litSegments(pct(50), 100, 10) == 5);
}

TEST_CASE("cpu ramp goes green to red", "[meter]") {
    const auto t = darkTheme();
    const auto low = segmentColor(t, MeterKind::Cpu, 0, 30);
    const auto high = segmentColor(t, MeterKind::Cpu, 29, 30);
    REQUIRE(low.g > low.r);
    REQUIRE(high.r > high.g);
}

TEST_CASE("non-cpu meters use the theme colour", "[meter]") {
    const auto t = darkTheme();
    REQUIRE(segmentColor(t, MeterKind::Memory, 5, 30) == t.memory);
    REQUIRE(segmentColor(t, MeterKind::Disk, 5, 30) == t.disk);
    REQUIRE(segmentColor(t, MeterKind::Network, 5, 30) == t.netRx);
}

TEST_CASE("phosphor cpu meter keeps one hue", "[meter]") {
    const auto t = greenPhosphorTheme();
    const auto c = segmentColor(t, MeterKind::Cpu, 29, 30);
    REQUIRE(c.g >= c.r);
    REQUIRE(c.g >= c.b);
}

TEST_CASE("easing approaches without overshoot", "[meter]") {
    const double one = easeToward(0, 100, 0.016);
    REQUIRE(one > 0);
    REQUIRE(one < 100);
    double v = 0;
    for (int i = 0; i < 200; ++i) v = easeToward(v, 100, 0.016);
    REQUIRE(v == Approx(100).margin(0.5));
    REQUIRE(v <= 100.0);
    REQUIRE(easeToward(42, 100, 0) == 42);
}
