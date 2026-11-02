// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/disk_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;
using pmp::linux::DiskProvider;

namespace {

void layout(FakeRoot& fr) {
    fr.write("sys/block/nvme0n1/device/model", "Fake NVMe\n");
    fr.write("sys/block/nvme0n1/size", "2000000\n");
    fr.write("sys/block/nvme0n1/queue/rotational", "0\n");
    fr.write("sys/block/sda/device/model", "Fake HDD\n");
    fr.write("sys/block/sda/size", "1000\n");
    fr.write("sys/block/sda/queue/rotational", "1\n");
    fr.write("sys/block/loop0/size", "10\n");
}

constexpr const char* kStats0 =
    " 259 0 nvme0n1 100 0 2000 50 200 0 4000 150 0 300 200\n"
    " 259 1 nvme0n1p1 90 0 1800 40 150 0 3000 100 0 250 150\n"
    "   8 0 sda 10 0 80 5 0 0 0 0 0 5 5\n"
    "   7 0 loop0 1 0 2 0 0 0 0 0 0 0 0\n";

std::string stats1(int nvmeSectorsRead) {
    return " 259 0 nvme0n1 150 0 " + std::to_string(nvmeSectorsRead) +
           " 100 250 0 6048 200 0 1300 400\n"
           " 259 1 nvme0n1p1 90 0 1800 40 150 0 3000 100 0 250 150\n"
           "   8 0 sda 10 0 80 5 0 0 0 0 0 5 5\n"
           "   7 0 loop0 1 0 2 0 0 0 0 0 0 0 0\n";
}

Frame sampleAt(DiskProvider& p, int gen, MonoTime t) {
    Frame f;
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), t});
    return f;
}

const DiskSnapshot& dev(const Frame& f, std::string_view name) {
    for (const auto& d : f.disks.devices) {
        if (d.name == name) return d;
    }
    FAIL("device not found: " << name);
    return f.disks.combined;
}

const MonoTime t0{};
const MonoTime t1 = t0 + 2s;

}  // namespace

TEST_CASE("disk devices are physical only", "[disk]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/diskstats", kStats0);
    DiskProvider p(fr.root());
    auto f = sampleAt(p, 1, t0);
    REQUIRE(f.disks.devices.size() == 2);
    REQUIRE(f.disks.devices[0].name == "nvme0n1");
    REQUIRE(f.disks.devices[1].name == "sda");
}

TEST_CASE("disk rates from deltas", "[disk]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/diskstats", kStats0);
    DiskProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/diskstats", stats1(4048));
    auto f = sampleAt(p, 2, t1);
    const auto& n = dev(f, "nvme0n1");
    REQUIRE(*n.readBps.value == Approx(524288));
    REQUIRE(*n.writeBps.value == Approx(524288));
    REQUIRE(*n.activePct.value == Approx(50.0));
    REQUIRE(*n.avgResponseMs.value == Approx(1.0));
    REQUIRE(*n.totalRead.value == 4048.0 * 512);
    REQUIRE(*n.capacity.value == 2000000.0 * 512);
    REQUIRE(n.model == "Fake NVMe");
    REQUIRE(n.type == DiskType::SSD);
}

TEST_CASE("idle disk response time is a measured zero", "[disk]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/diskstats", kStats0);
    DiskProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/diskstats", stats1(4048));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(dev(f, "sda").avgResponseMs.isMeasured());
    REQUIRE(*dev(f, "sda").avgResponseMs.value == 0.0);
}

TEST_CASE("combined disk view", "[disk]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/diskstats", kStats0);
    DiskProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/diskstats", stats1(4048));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(f.disks.combined.name == "combined");
    REQUIRE(*f.disks.combined.readBps.value == Approx(524288));
    REQUIRE(*f.disks.combined.activePct.value == Approx(50.0));
    REQUIRE(f.disks.combined.type == DiskType::Mixed);
    REQUIRE(*f.disks.combined.capacity.value == (2000000.0 + 1000) * 512);
}

TEST_CASE("disk counter reset yields Pending", "[disk]") {
    FakeRoot fr;
    layout(fr);
    fr.write("proc/diskstats", kStats0);
    DiskProvider p(fr.root());
    sampleAt(p, 1, t0);
    fr.write("proc/diskstats", stats1(10));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(dev(f, "nvme0n1").readBps.availability == Availability::Pending);
}

TEST_CASE("combined is Unsupported when there are no physical disks", "[disk]") {
    FakeRoot fr;
    fr.write("sys/block/loop0/size", "10\n");
    fr.write("proc/diskstats", kStats0);
    DiskProvider p(fr.root());
    sampleAt(p, 1, t0);
    auto f = sampleAt(p, 2, t1);
    REQUIRE(f.disks.combined.totalRead.availability == Availability::Unsupported);
    REQUIRE(f.disks.combined.readBps.availability == Availability::Unsupported);
}

TEST_CASE("a disk missing from diskstats makes combined Unsupported, not partial", "[disk]") {
    FakeRoot fr;
    layout(fr);
    const char* onlyNvme = " 259 0 nvme0n1 100 0 2000 50 200 0 4000 150 0 300 200\n";
    fr.write("proc/diskstats", onlyNvme);
    DiskProvider p(fr.root());
    sampleAt(p, 1, t0);
    auto f = sampleAt(p, 2, t1);
    REQUIRE(f.disks.combined.totalRead.availability == Availability::Unsupported);
    REQUIRE(f.disks.combined.readBps.availability == Availability::Unsupported);
    REQUIRE(dev(f, "nvme0n1").totalRead.isMeasured());
}

TEST_CASE("an unreadable block device directory does not fail the provider", "[disk]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    layout(fr);
    fr.write("proc/diskstats", kStats0);
    fr.makeUnreadable("sys/block/sda");
    DiskProvider p(fr.root());
    Frame f;
    REQUIRE_NOTHROW(f = sampleAt(p, 1, t0));
    REQUIRE(f.disks.devices.size() == 1);
}
