// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/bench_model.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::linux;

namespace {

BenchResult result(BenchId id, double raw) {
    BenchResult r;
    r.id = id;
    r.raw = raw;
    r.rawUnit = rawUnitOf(id);
    r.score = scoreFor(id, raw);
    return r;
}

BenchRecord record(std::int64_t wall, double cpu, double latency = 118, const std::string& model = "Test CPU", int version = kBenchVersion) {
    BenchRecord r;
    r.benchVersion = version;
    r.cpuModel = model;
    r.logicalCpus = 8;
    r.wallMs = wall;
    r.results = {result(BenchId::CpuSingle, cpu), result(BenchId::MemLatency, latency)};
    return r;
}

constexpr std::int64_t kOct20 = 1'760'918'400'000LL;  // 2025-10-20
constexpr std::int64_t kOct25 = kOct20 + 5 * 86'400'000LL;

}  // namespace

TEST_CASE("compare text uses a true minus sign", "[bench-vm]") {
    REQUIRE(compareText(112, 100, "2026-10-20", false) == "+12% vs your run on 2026-10-20");
    REQUIRE(compareText(96, 100, "2026-10-20", false) == "\xE2\x88\x92" "4% vs your run on 2026-10-20");
    REQUIRE(compareText(100.2, 100, "2026-10-20", false) == "No change vs your run on 2026-10-20");
    REQUIRE(compareText(100, 0, "x", false).empty());
    // for latency a lower number is the improvement
    REQUIRE(compareText(90, 100, "2026-10-20", true) == "+11% vs your run on 2026-10-20");
    REQUIRE(compareText(110, 100, "2026-10-20", true) == "\xE2\x88\x92" "9% vs your run on 2026-10-20");
}

TEST_CASE("raw metrics read naturally", "[bench-vm]") {
    REQUIRE(rawText(result(BenchId::MemBandwidth, 31.24)) == "31.2 GB/s");
    REQUIRE(rawText(result(BenchId::MemLatency, 118.04)) == "118.0 ns");
    REQUIRE(rawText(result(BenchId::DiskSeqWrite, 1580.4)) == "1,580 MB/s");
    REQUIRE(rawText(result(BenchId::DiskRandRead, 18464.6)) == "18,465 IOPS");
    REQUIRE(rawText(result(BenchId::CpuSingle, 2668.3)) == "2,668 Mops/s");
}

TEST_CASE("the first run on a machine says so", "[bench-vm]") {
    const auto latest = record(kOct25, 2650);
    const auto cards = buildCards(latest, {latest});
    REQUIRE(cards.size() == 2);
    REQUIRE(cards[0].name == "CPU single-thread");
    REQUIRE(cards[0].score == "1,000");
    REQUIRE(cards[0].raw == "2,650 Mops/s");
    REQUIRE(cards[0].date == "2025-10-25");
    REQUIRE(cards[0].comparison == "First run on this machine");
    REQUIRE_FALSE(cards[0].comparable);
}

TEST_CASE("compare with your previous comparable run", "[bench-vm]") {
    const auto older = record(kOct20, 2500);
    const auto latest = record(kOct25, 2800);
    const auto cards = buildCards(latest, {older, latest});
    REQUIRE(cards[0].comparison == "+12% vs your run on 2025-10-20");
    REQUIRE(cards[0].comparable);
    REQUIRE(cards[1].comparison == "No change vs your run on 2025-10-20");  // same latency
}

TEST_CASE("runs on another cpu or an older test version are not compared", "[bench-vm]") {
    const auto otherCpu = record(kOct20, 100, 118, "Another CPU");
    const auto oldVersion = record(kOct20, 100, 118, "Test CPU", kBenchVersion + 1);
    const auto latest = record(kOct25, 2800);
    const auto cards = buildCards(latest, {otherCpu, oldVersion, latest});
    REQUIRE(cards[0].comparison == "First run on this machine");
    REQUIRE_FALSE(comparable(latest, otherCpu));
    REQUIRE_FALSE(comparable(latest, oldVersion));
    REQUIRE(comparable(latest, record(kOct20, 1)));
}

TEST_CASE("the newest earlier run is the one compared", "[bench-vm]") {
    const auto a = record(kOct20, 1000);
    const auto b = record(kOct20 + 86'400'000, 2000);
    const auto latest = record(kOct25, 2200);
    const auto cards = buildCards(latest, {a, b, latest});
    REQUIRE(cards[0].comparison == "+10% vs your run on 2025-10-21");
}

TEST_CASE("a cancelled or failed test has no score and says why", "[bench-vm]") {
    auto latest = record(kOct25, 2650);
    latest.results[0].cancelled = true;
    latest.results[0].raw = 0;
    latest.results[1].raw = 0;
    latest.results[1].note = "data mismatch";
    const auto cards = buildCards(latest, {latest});
    REQUIRE(cards[0].failed);
    REQUIRE(cards[0].score == "—");
    REQUIRE(cards[0].raw == "Cancelled");
    REQUIRE(cards[1].failed);
    REQUIRE(cards[1].raw == "data mismatch");
}

TEST_CASE("a cancelled earlier result is not used for comparison", "[bench-vm]") {
    auto older = record(kOct20, 2500);
    older.results[0].cancelled = true;
    const auto latest = record(kOct25, 2800);
    REQUIRE(buildCards(latest, {older, latest})[0].comparison == "First run on this machine");
}

TEST_CASE("history lines", "[bench-vm]") {
    auto r = record(kOct25, 2650);
    r.results[1].cancelled = true;
    REQUIRE(historyLine(r) == "2025-10-25 · CPU single-thread 1,000 · Memory latency —");
}

TEST_CASE("a debug build's run is never compared with an optimised build's", "[bench-vm][minor]") {
    auto debug = record(kOct20, 100);
    debug.optimized = false;
    auto release = record(kOct25, 2650);
    release.optimized = true;
    REQUIRE_FALSE(comparable(debug, release));
    REQUIRE(buildCards(release, {debug, release})[0].comparison == "First run on this machine");
    auto debug2 = debug;
    debug2.wallMs = kOct25;
    REQUIRE(comparable(debug, debug2));
}
