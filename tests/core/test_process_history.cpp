// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/process_history.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;

namespace {

MonoTime at(int s) { return MonoTime{} + std::chrono::seconds(s); }

ProcessRecord proc(int pid, std::uint64_t start, double cpu, double mem = 1000, double read = 0, double write = 0) {
    ProcessRecord r;
    r.key = {pid, start};
    r.name = "p" + std::to_string(pid);
    r.cpu = cpu < 0 ? Metric::notMeasured(Availability::Pending, Unit::Percent) : Metric::measured(cpu, Unit::Percent);
    r.memFootprint = Metric::measured(mem, Unit::Bytes);
    r.diskReadBps = Metric::measured(read, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(write, Unit::BytesPerSec);
    return r;
}

Frame frame(int t, std::vector<ProcessRecord> ps) {
    Frame f;
    f.mono = at(t);
    f.processes.processes = std::move(ps);
    return f;
}

double value(const HistoryRing<double>& r, std::size_t i) { return *r.at(i).value; }

}  // namespace

TEST_CASE("rings record cpu, memory and disk per process", "[process-history]") {
    ProcessHistory h(10);
    h.ingest(frame(0, {proc(1, 1, 25, 500, 100, 50)}), false);
    h.ingest(frame(1, {proc(1, 1, 30, 600, 10, 5)}), false);
    const auto cpu = h.cpuSeries({1, 1});
    const auto mem = h.memorySeries({1, 1});
    const auto disk = h.diskSeries({1, 1});
    REQUIRE(cpu);
    REQUIRE(cpu->size() == 2);
    REQUIRE(value(*cpu, 1) == 30);
    REQUIRE(value(*mem, 0) == 500);
    REQUIRE(value(*disk, 0) == 150);
    REQUIRE_FALSE(h.cpuSeries({2, 2}));
}

TEST_CASE("unmeasured is a gap, never zero", "[process-history]") {
    ProcessHistory h(10);
    h.ingest(frame(0, {proc(1, 1, -1)}), false);
    h.ingest(frame(1, {proc(1, 1, 10)}), false);
    const auto cpu = h.cpuSeries({1, 1});
    REQUIRE_FALSE(cpu->at(0).value.has_value());
    REQUIRE(cpu->at(1).value == 10.0);
}

TEST_CASE("a recycled pid is a different history", "[process-history]") {
    ProcessHistory h(10);
    h.ingest(frame(0, {proc(7, 100, 5)}), false);
    h.ingest(frame(1, {proc(7, 200, 6)}), false);
    REQUIRE(h.tracked() == 2);
    REQUIRE(h.cpuSeries({7, 100})->size() == 1);
    REQUIRE(h.cpuSeries({7, 200})->size() == 1);
}

TEST_CASE("an exited process keeps its history until it ages out", "[process-history]") {
    ProcessHistory h(5);
    h.ingest(frame(0, {proc(1, 1, 10), proc(2, 2, 10)}), false);
    for (int t = 1; t <= 4; ++t) h.ingest(frame(t, {proc(1, 1, 10)}), false);
    REQUIRE(h.cpuSeries({2, 2}));  // gone for 4 frames: still retained
    const auto c = h.culprits(at(0), at(4), ProcessHistory::Resource::Cpu, 1, 10);
    bool exitedFlag = false;
    for (const auto& x : c) exitedFlag = exitedFlag || (x.key.pid == 2 && x.exited);
    REQUIRE(exitedFlag);
    h.ingest(frame(5, {proc(1, 1, 10)}), false);
    REQUIRE_FALSE(h.cpuSeries({2, 2}));
    REQUIRE(h.tracked() == 1);
}

TEST_CASE("culprits rank by integrated cpu seconds including exited processes", "[process-history]") {
    ProcessHistory h(20);
    for (int t = 0; t <= 3; ++t) {
        std::vector<ProcessRecord> ps{proc(1, 1, 50)};
        if (t <= 2) ps.push_back(proc(2, 2, 100));  // exits after t = 2
        h.ingest(frame(t, ps), false);
    }
    const auto c = h.culprits(at(0), at(3), ProcessHistory::Resource::Cpu, 4, 10);
    REQUIRE(c.size() == 2);
    REQUIRE(c[0].key.pid == 2);
    REQUIRE(c[0].exited);
    REQUIRE(c[0].cpuSeconds == Approx(8.0));   // 100% of 4 CPUs for 2 s
    REQUIRE(c[1].key.pid == 1);
    REQUIRE_FALSE(c[1].exited);
    REQUIRE(c[1].cpuSeconds == Approx(6.0));   // 50% of 4 CPUs for 3 s
    REQUIRE(c[1].avgCpuPct == Approx(50.0));
}

TEST_CASE("memory culprits show average and peak", "[process-history]") {
    ProcessHistory h(20);
    h.ingest(frame(0, {proc(1, 1, 1, 100), proc(2, 2, 1, 50)}), false);
    h.ingest(frame(1, {proc(1, 1, 1, 300), proc(2, 2, 1, 60)}), false);
    const auto c = h.culprits(at(0), at(1), ProcessHistory::Resource::Memory, 1, 10);
    REQUIRE(c[0].key.pid == 1);
    REQUIRE(c[0].peakMemBytes == 300);
    REQUIRE(c[0].avgMemBytes == Approx(200));
}

TEST_CASE("a range outside retained history yields nothing", "[process-history]") {
    ProcessHistory h(10);
    h.ingest(frame(10, {proc(1, 1, 50)}), false);
    h.ingest(frame(11, {proc(1, 1, 50)}), false);
    REQUIRE(h.culprits(at(0), at(5), ProcessHistory::Resource::Cpu, 1, 10).empty());
}

TEST_CASE("a gap in the range is not integrated", "[process-history]") {
    ProcessHistory h(10);
    h.ingest(frame(0, {proc(1, 1, 100)}), false);
    h.ingest(frame(1, {proc(1, 1, 100)}), false);
    h.ingest(frame(10, {proc(1, 1, 100)}), true);  // a suspend: gapBefore
    const auto c = h.culprits(at(0), at(10), ProcessHistory::Resource::Cpu, 1, 10);
    REQUIRE(c[0].cpuSeconds == Approx(1.0));       // only the 0->1 s interval; the 9 s hole is not invented
}

TEST_CASE("per-process history fits the memory budget", "[process-history]") {
    ProcessHistory h(300);
    std::vector<ProcessRecord> ps;
    for (int i = 0; i < 1200; ++i) ps.push_back(proc(i + 1, 1, 1));
    h.ingest(frame(0, ps), false);
    REQUIRE(h.tracked() == 1200);
    REQUIRE(static_cast<double>(h.approxBytes()) / (1200.0 * 300.0) <= 64.0);
}

TEST_CASE("the tracked count is capped and keeps the busiest", "[process-history]") {
    ProcessHistory h(10, 10);
    std::vector<ProcessRecord> ps;
    for (int i = 0; i < 50; ++i) ps.push_back(proc(i + 1, 1, static_cast<double>(i)));
    h.ingest(frame(0, ps), false);
    REQUIRE(h.tracked() == 10);
    REQUIRE(h.cpuSeries({50, 1}));   // the busiest are kept
    REQUIRE_FALSE(h.cpuSeries({1, 1}));
}

TEST_CASE("coverage starts at the oldest retained sample", "[process-history]") {
    ProcessHistory h(3);
    REQUIRE_FALSE(h.coverageStart().has_value());
    for (int t = 0; t < 5; ++t) h.ingest(frame(t, {proc(1, 1, 10)}), false);
    REQUIRE(h.coverageStart() == at(2));  // capacity 3: samples at 2, 3, 4 remain
}
