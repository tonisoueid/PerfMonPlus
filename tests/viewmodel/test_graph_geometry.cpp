// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/graph_geometry.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace std::chrono_literals;
using Catch::Approx;

namespace {

const MonoTime now = MonoTime{} + 1000s;
const PlotSize size{600, 100};
const Scale scale{0, 100};

MonoTime ago(int sec) { return now - std::chrono::seconds(sec); }

}  // namespace

TEST_CASE("newest sample sits on the right edge", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(now, 100);
    const auto t = buildTrace(r, now, 60s, size, scale);
    REQUIRE(t.runs.size() == 1);
    REQUIRE(t.runs[0][0].x == Approx(600));
    REQUIRE(t.runs[0][0].y == Approx(0));
}

TEST_CASE("value maps to height", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(ago(1), 0);
    r.push(now, 50);
    const auto t = buildTrace(r, now, 60s, size, scale);
    REQUIRE(t.runs[0][0].y == Approx(100));
    REQUIRE(t.runs[0][1].y == Approx(50));
}

TEST_CASE("older samples scroll left", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(ago(30), 10);
    const auto t = buildTrace(r, now, 60s, size, scale);
    REQUIRE(t.runs[0][0].x == Approx(300));
}

TEST_CASE("gap splits the trace", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(ago(20), 10);
    r.pushGap(ago(10));
    r.push(now, 20);
    const auto t = buildTrace(r, now, 60s, size, scale);
    REQUIRE(t.runs.size() == 2);
    REQUIRE(t.runs[0].size() == 1);
    REQUIRE(t.runs[1].size() == 1);
}

TEST_CASE("unavailable series draws nothing", "[geometry]") {
    HistoryRing<double> r(10);
    r.pushGap(ago(2));
    r.pushGap(now);
    REQUIRE(buildTrace(r, now, 60s, size, scale).runs.empty());
}

TEST_CASE("samples outside the window are dropped", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(ago(90), 10);
    REQUIRE(buildTrace(r, now, 60s, size, scale).runs.empty());
}

TEST_CASE("y is clamped to the plot", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(now, 500);
    REQUIRE(buildTrace(r, now, 60s, size, scale).runs[0][0].y == Approx(0));
}

TEST_CASE("autoScale pads the maximum and honours minSpan", "[geometry]") {
    HistoryRing<double> r(10);
    r.push(ago(2), 20);
    r.push(now, 80);
    const auto s = autoScale(r, now, 60s, 10);
    REQUIRE(s.lo == Approx(0));
    REQUIRE(s.hi == Approx(88));

    HistoryRing<double> z(10);
    z.push(now, 0);
    REQUIRE(autoScale(z, now, 60s, 10).hi == Approx(10));
}

TEST_CASE("lower frame rates scroll in coarser steps", "[geometry]") {
    REQUIRE(scrollQuantumPx(120) == Approx(0.5));
    REQUIRE(scrollQuantumPx(60) == Approx(0.5));
    REQUIRE(scrollQuantumPx(30) == Approx(1.0));
    REQUIRE(scrollQuantumPx(15) == Approx(2.0));
    REQUIRE(scrollQuantumPx(0) == Approx(2.0));  // nonsense never divides or disables repainting
}
