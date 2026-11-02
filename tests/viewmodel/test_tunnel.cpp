// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/tunnel.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;

namespace {
double meanAlpha(const std::vector<TunnelRing>& rings, int channel) {
    double sum = 0;
    int n = 0;
    for (const auto& r : rings) {
        if (r.channel == channel) {
            sum += r.alpha;
            ++n;
        }
    }
    return n ? sum / n : 0;
}
}  // namespace

TEST_CASE("rings stay inside the frame", "[tunnel]") {
    for (double t : {0.0, 0.37, 5.0, 1234.5}) {
        for (const auto& r : tunnelRings(t, {1, 1, 1, 1})) {
            REQUIRE(r.scale >= 0.0);
            REQUIRE(r.scale <= 1.0);
            REQUIRE(r.alpha >= 0.0);
            REQUIRE(r.alpha <= 1.0);
        }
    }
}

TEST_CASE("channels cycle", "[tunnel]") {
    const auto rings = tunnelRings(1.0, {});
    REQUIRE(rings.size() == 24);
    for (int i = 0; i < 8; ++i) REQUIRE(rings[static_cast<std::size_t>(i)].channel == i % 4);
}

TEST_CASE("a busy channel is brighter", "[tunnel]") {
    const auto busy = tunnelRings(2.0, {1, 0, 0, 0});
    const auto idle = tunnelRings(2.0, {0, 0, 0, 0});
    REQUIRE(meanAlpha(busy, 0) > meanAlpha(idle, 0));
    REQUIRE(meanAlpha(busy, 0) > meanAlpha(busy, 1));
}

TEST_CASE("the tunnel moves over time even when idle", "[tunnel]") {
    const auto a = tunnelRings(0.0, {});
    const auto b = tunnelRings(0.5, {});
    REQUIRE(a[0].scale != b[0].scale);
}

TEST_CASE("an empty frame is all idle", "[tunnel]") {
    const auto l = channelLevels(Frame{});
    REQUIRE(l.cpu == 0);
    REQUIRE(l.memory == 0);
    REQUIRE(l.gpu == 0);
    REQUIRE(l.disk == 0);
}

TEST_CASE("channel levels come from the frame", "[tunnel]") {
    Frame f = pmp::testing::populatedFrame(4, 3);
    pmp::testing::withHardware(f);
    f.memory.total = Metric::measured(1000, Unit::Bytes);
    f.memory.inUse = Metric::measured(250, Unit::Bytes);
    f.disks.combined.activePct = Metric::measured(40, Unit::Percent);
    const auto l = channelLevels(f);
    REQUIRE(l.cpu > 0.15);
    REQUIRE(l.cpu < 0.17);
    REQUIRE(l.memory == 0.25);
    REQUIRE(l.gpu > 0.17);
    REQUIRE(l.gpu < 0.19);
    REQUIRE(l.disk == 0.4);
}
