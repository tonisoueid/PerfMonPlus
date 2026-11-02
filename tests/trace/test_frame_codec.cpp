// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/frame_codec.hpp>

#include "support/full_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace pmp;
using namespace pmp::trace;
using namespace pmp::testing;

#if defined(__GLIBCXX__) && defined(__x86_64__)
// Adding a field to one of these structs changes its size and stops the build here, reminding whoever did it to
// add the field to visit() in trace/src/frame_codec.cpp and to fullFrame()/sameRecorded() in tests/support.
static_assert(sizeof(LogicalCpu) == 288 && sizeof(CpuSnapshot) == 400 && sizeof(MemorySnapshot) == 728);
static_assert(sizeof(DiskSnapshot) == 464 && sizeof(NetInterface) == 264 && sizeof(GpuSnapshot) == 472);
static_assert(sizeof(NpuSnapshot) == 96 && sizeof(BatteryInfo) == 608 && sizeof(EnergySnapshot) == 760);
static_assert(sizeof(ThermalSnapshot) == 112 && sizeof(PowerDomain) == 120 && sizeof(GpuEngine) == 88);
#endif

TEST_CASE("round trip preserves every metric", "[trace][codec]") {
    const auto f = fullFrame();
    const auto s = flatten(f, false, 5);
    const auto back = unflatten(s, 99);
    std::string why;
    INFO("first difference: " << why);
    REQUIRE(sameRecorded(f, back, &why));
    REQUIRE(back.generation == 99);
    REQUIRE(back.wall == f.wall);
    REQUIRE(back.mono == f.mono);
}

TEST_CASE("availability survives and a measured zero stays a zero", "[trace][codec]") {
    auto f = fullFrame();
    f.cpu.total = Metric::measured(0.0, Unit::Percent);
    f.cpu.kernel = Metric::notMeasured(Availability::Restricted, Unit::Percent);
    f.cpu.freqHz = Metric::notMeasured(Availability::Unsupported, Unit::Hertz);
    f.memory.total = Metric::notMeasured(Availability::Pending, Unit::Bytes);
    f.memory.inUse = Metric::notMeasured(Availability::Stale, Unit::Bytes);
    f.memory.cached = Metric::notMeasured(Availability::Failed, Unit::Bytes);
    f.memory.buffers = Metric::notMeasured(Availability::Disabled, Unit::Bytes);
    const auto back = unflatten(flatten(f, false, 3), 1);
    REQUIRE(back.cpu.total.availability == Availability::Measured);
    REQUIRE(*back.cpu.total.value == 0.0);
    REQUIRE(back.cpu.kernel.availability == Availability::Restricted);
    REQUIRE_FALSE(back.cpu.kernel.value.has_value());
    REQUIRE(back.cpu.freqHz.availability == Availability::Unsupported);
    REQUIRE(back.memory.total.availability == Availability::Pending);
    REQUIRE(back.memory.inUse.availability == Availability::Stale);
    REQUIRE(back.memory.cached.availability == Availability::Failed);
    REQUIRE(back.memory.buffers.availability == Availability::Disabled);
    REQUIRE(sameRecorded(f, back));
}

TEST_CASE("only the top processes are kept", "[trace][codec]") {
    auto f = fullFrame(40);
    f.processes.processes[3].exitedAt = MonoTime{};  // a tombstone is never recorded
    const auto s = flatten(f, false, 5);
    REQUIRE(s.procs.size() <= 10);
    REQUIRE(s.procs.size() >= 5);
    std::vector<const ProcessRecord*> live;
    for (const auto& p : f.processes.processes) {
        if (!p.exitedAt) live.push_back(&p);
    }
    std::sort(live.begin(), live.end(), [](auto* a, auto* b) { return *a->cpu.value > *b->cpu.value; });
    std::set<int> kept;
    for (const auto& p : s.procs) kept.insert(p.pid);
    for (int i = 0; i < 5; ++i) REQUIRE(kept.contains(live[static_cast<std::size_t>(i)]->key.pid));
    REQUIRE_FALSE(kept.contains(f.processes.processes[3].key.pid));
    const auto back = unflatten(s, 1);
    REQUIRE(back.processes.processes.size() == s.procs.size());
    REQUIRE(*back.processes.count.value == 40);  // the recorded total, not the row count
    REQUIRE(back.processes.processes[0].key.pid == s.procs[0].pid);
}

TEST_CASE("flatten order is deterministic", "[trace][codec]") {
    const auto f = fullFrame(40);
    const auto a = flatten(f, false, 8), b = flatten(f, false, 8);
    REQUIRE(a.procs.size() == b.procs.size());
    for (std::size_t i = 0; i < a.procs.size(); ++i) REQUIRE(a.procs[i].pid == b.procs[i].pid);
    REQUIRE(a.values.size() == b.values.size());
}

TEST_CASE("no personal data is recorded", "[trace][codec]") {
    const auto s = flatten(fullFrame(), false, 10);
    auto scan = [](const std::string& str) {
        REQUIRE(str.find("alice") == std::string::npos);
        REQUIRE(str.find("secret") == std::string::npos);
        REQUIRE(str.find("--token") == std::string::npos);
        REQUIRE(str.find("/home") == std::string::npos);
    };
    for (const auto& t : s.texts) {
        scan(t.id);
        scan(t.value);
    }
    for (const auto& v : s.values) scan(v.id);
    for (const auto& p : s.procs) scan(p.name);
}

TEST_CASE("names are sanitised and bounded", "[trace][codec]") {
    auto f = fullFrame(3);
    f.processes.processes[0].name = std::string("evil\x01\x02name\xFF") + std::string(10000, 'x');
    f.processes.processes[1].name = "caf\xC3\xA9";
    const auto s = flatten(f, false, 3);
    for (const auto& p : s.procs) {
        REQUIRE(p.name.size() <= kMaxTextBytes);
        for (const char ch : p.name) REQUIRE(static_cast<unsigned char>(ch) >= 0x20);
    }
    bool sawCafe = false, sawEvil = false;
    for (const auto& p : s.procs) {
        sawCafe |= p.name == "caf\xC3\xA9";
        sawEvil |= p.name.starts_with("evil??name?x");
    }
    REQUIRE(sawCafe);
    REQUIRE(sawEvil);
    REQUIRE(sanitize(std::string(300, 'a')).size() <= kMaxTextBytes);
    REQUIRE(sanitize(std::string(300, 'a')).ends_with("\xE2\x80\xA6"));
    REQUIRE(sanitize("short") == "short");
}

TEST_CASE("unknown series are ignored and missing ones are unsupported", "[trace][codec]") {
    auto s = flatten(fullFrame(), false, 3);
    s.values.push_back({"future.thing", Unit::Count, Availability::Measured, 5.0});
    s.texts.push_back({"future.text", "x"});
    s.values.erase(std::remove_if(s.values.begin(), s.values.end(), [](const SeriesValue& v) { return v.id == "mem.cached"; }),
                   s.values.end());
    const auto back = unflatten(s, 1);
    REQUIRE(back.memory.cached.availability == Availability::Unsupported);
    REQUIRE(back.cpu.total.isMeasured());
}

TEST_CASE("an empty sample unflattens to an empty frame without crashing", "[trace][codec]") {
    const auto back = unflatten(TraceSample{}, 1);
    REQUIRE(back.cpu.logical.empty());
    REQUIRE(back.gpus.empty());
    REQUIRE(back.cpu.total.availability == Availability::Unsupported);
}

TEST_CASE("garbage enum values do not escape", "[trace][codec]") {
    auto s = flatten(fullFrame(), false, 3);
    for (auto& v : s.values) {
        if (v.id == "cpu.total") v.avail = static_cast<Availability>(200);
    }
    const auto back = unflatten(s, 1);
    REQUIRE(back.cpu.total.availability == Availability::Failed);
}
