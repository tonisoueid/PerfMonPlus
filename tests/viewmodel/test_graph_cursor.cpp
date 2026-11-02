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
MonoTime at(int ms) { return MonoTime{} + std::chrono::milliseconds(ms); }
const PlotSize kSize{600, 100};
const auto kWindow = 60000ms;  // 10 ms per pixel
}  // namespace

TEST_CASE("the right edge is now and the left edge is now minus the window", "[graph-cursor]") {
    const MonoTime now = at(100000);
    REQUIRE(*timeAtX(600, kSize, now, kWindow) == now);
    REQUIRE(*timeAtX(0, kSize, now, kWindow) == now - kWindow);
    REQUIRE(*timeAtX(300, kSize, now, kWindow) == now - 30s);
}

TEST_CASE("x and time round trip, and agree with buildTrace", "[graph-cursor]") {
    const MonoTime now = at(100000);
    for (double x : {0.0, 1.5, 123.0, 599.0, 600.0}) {
        const auto t = timeAtX(x, kSize, now, kWindow);
        REQUIRE(t);
        REQUIRE(*xAtTime(*t, kSize, now, kWindow) == Approx(x).margin(1e-6));
    }
    HistoryRing<double> ring(10);
    ring.push(at(70000), 5.0);
    const auto trace = buildTrace(ring, now, kWindow, kSize, {0, 10});
    REQUIRE(trace.runs[0][0].x == Approx(*xAtTime(at(70000), kSize, now, kWindow)));
}

TEST_CASE("outside the plot has no time", "[graph-cursor]") {
    const MonoTime now = at(100000);
    REQUIRE_FALSE(timeAtX(-1, kSize, now, kWindow).has_value());
    REQUIRE_FALSE(timeAtX(601, kSize, now, kWindow).has_value());
}

TEST_CASE("a time older than the window or in the future has no x", "[graph-cursor]") {
    const MonoTime now = at(100000);
    REQUIRE_FALSE(xAtTime(now - kWindow - 1ms, kSize, now, kWindow).has_value());
    REQUIRE_FALSE(xAtTime(now + 1s, kSize, now, kWindow).has_value());
    REQUIRE(xAtTime(now - kWindow, kSize, now, kWindow).has_value());
}

TEST_CASE("sampleNear returns the nearest slot within tolerance", "[graph-cursor]") {
    HistoryRing<double> ring(10);
    ring.push(at(1000), 1.0);
    ring.pushGap(at(2000));
    ring.push(at(3000), 3.0);
    const auto a = sampleNear(ring, at(1100), 500ms);
    REQUIRE(a);
    REQUIRE(a->t == at(1000));
    REQUIRE(*a->value == 1.0);
    const auto gap = sampleNear(ring, at(2100), 500ms);
    REQUIRE(gap);
    REQUIRE_FALSE(gap->value.has_value());  // a gap is reported as a gap, never as zero
    REQUIRE_FALSE(sampleNear(ring, at(10000), 500ms).has_value());  // beyond tolerance of anything
    REQUIRE_FALSE(sampleNear(HistoryRing<double>(3), at(0), 500ms).has_value());
}

TEST_CASE("ranges are ordered whichever way they are dragged", "[graph-cursor]") {
    const auto r = orderedRange(at(5000), at(2000));
    REQUIRE(r.from == at(2000));
    REQUIRE(r.to == at(5000));
}

TEST_CASE("a range is clipped to the window and vanishes when fully outside", "[graph-cursor]") {
    const MonoTime now = at(100000);
    const auto clipped = clipToWindow({now - 90s, now - 10s}, now, kWindow);
    REQUIRE(clipped);
    REQUIRE(clipped->from == now - kWindow);
    REQUIRE(clipped->to == now - 10s);
    REQUIRE_FALSE(clipToWindow({now - 120s, now - 70s}, now, kWindow).has_value());
    REQUIRE_FALSE(clipToWindow({now + 1s, now + 5s}, now, kWindow).has_value());
}

TEST_CASE("a selection stays anchored to absolute time as now advances", "[graph-cursor]") {
    const MonoTime t = at(90000);
    const double x0 = *xAtTime(t, kSize, at(100000), kWindow);
    const double x1 = *xAtTime(t, kSize, at(105000), kWindow);
    REQUIRE(x0 - x1 == Approx(5.0 * 10.0));  // 5 s later it has moved 50 px to the left
}
