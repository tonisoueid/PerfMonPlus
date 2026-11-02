// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/net_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;
using pmp::linux::NetProvider;

namespace {

std::string netDev(long loBytes, long ethRx, long ethTx) {
    return "Inter-|   Receive                                                |  Transmit\n"
           " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
           "    lo: " + std::to_string(loBytes) + " 10 0 0 0 0 0 0 " + std::to_string(loBytes) +
           " 10 0 0 0 0 0 0\n"
           "  eth0: " + std::to_string(ethRx) + " 10 0 0 0 0 0 0 " + std::to_string(ethTx) +
           " 5 0 0 0 0 0 0\n";
}

void layout(FakeRoot& fr) {
    fr.makeDir("sys/class/net/eth0/device");
    fr.write("sys/class/net/eth0/operstate", "up\n");
    fr.write("sys/class/net/lo/operstate", "unknown\n");
}

const NetInterface& iface(const Frame& f, std::string_view n) {
    for (const auto& i : f.net.interfaces) {
        if (i.name == n) return i;
    }
    FAIL("interface not found: " << n);
    return f.net.combined;
}

Frame sampleAt(NetProvider& p, int gen, MonoTime t) {
    Frame f;
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), t});
    return f;
}

const MonoTime t0{};
const MonoTime t1 = t0 + 1s;

}  // namespace

TEST_CASE("virtual interfaces are detected", "[net]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/net/dev", netDev(9000, 1000, 500));
    NetProvider p(fr.root());
    auto f = sampleAt(p, 1, t0);
    REQUIRE(iface(f, "lo").isVirtual);
    REQUIRE_FALSE(iface(f, "eth0").isVirtual);
    REQUIRE(iface(f, "eth0").up);
    REQUIRE_FALSE(iface(f, "lo").up);
}

TEST_CASE("network rates and totals", "[net]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/net/dev", netDev(9000, 1000, 500));
    NetProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/net/dev", netDev(19000, 5096, 1524));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(*iface(f, "eth0").rxBps.value == Approx(4096));
    REQUIRE(*iface(f, "eth0").txBps.value == Approx(1024));
    REQUIRE(*iface(f, "eth0").totalRx.value == 5096);
}

TEST_CASE("combined excludes virtual interfaces", "[net]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/net/dev", netDev(9000, 1000, 500));
    NetProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/net/dev", netDev(19000, 5096, 1524));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(f.net.combined.name == "combined");
    REQUIRE(*f.net.combined.rxBps.value == Approx(4096));
}

TEST_CASE("network counter reset yields Pending", "[net]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/net/dev", netDev(9000, 1000, 500));
    NetProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/net/dev", netDev(19000, 10, 1524));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(iface(f, "eth0").rxBps.availability == Availability::Pending);
    REQUIRE(f.net.combined.rxBps.availability == Availability::Pending);
}

TEST_CASE("combined is Unsupported when no interface is physical", "[net]") {
    FakeRoot fr;
    fr.write("sys/class/net/lo/operstate", "unknown\n");
    const auto only = [](long b) {
        return "h1\nh2\n    lo: " + std::to_string(b) + " 10 0 0 0 0 0 0 " + std::to_string(b) + " 10 0 0 0 0 0 0\n";
    };
    fr.write("proc/net/dev", only(100));
    NetProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/net/dev", only(9000));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(f.net.combined.rxBps.availability == Availability::Unsupported);
    REQUIRE(f.net.combined.totalRx.availability == Availability::Unsupported);
}

TEST_CASE("an unreadable interface directory does not fail the provider", "[net]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    layout(fr);
    fr.write("proc/net/dev", netDev(9000, 1000, 500));
    fr.makeUnreadable("sys/class/net/eth0");
    NetProvider p(fr.root());
    REQUIRE_NOTHROW(sampleAt(p, 1, t0));
}
