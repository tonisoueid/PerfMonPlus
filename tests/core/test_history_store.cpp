// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace pmp;
using namespace std::chrono_literals;

namespace {

Frame frameAt(int sec, double cpu) {
    Frame f;
    f.mono = MonoTime{} + std::chrono::seconds(sec);
    f.cpu.total = Metric::measured(cpu, Unit::Percent);
    return f;
}

}  // namespace

TEST_CASE("measured values are stored", "[history]") {
    HistoryStore h;
    h.ingest(frameAt(0, 40), false);
    REQUIRE(h.series(Series::CpuTotal).at(0).value == 40.0);
}

TEST_CASE("pending and unsupported are gaps, never zero", "[history]") {
    HistoryStore h;
    h.ingest(Frame{}, false);
    for (auto s : {Series::CpuTotal, Series::CpuKernel, Series::MemoryUsedBytes, Series::DiskActivePct,
                   Series::DiskReadBps, Series::DiskWriteBps, Series::NetRxBps, Series::NetTxBps}) {
        REQUIRE(h.series(s).size() == 1);
        REQUIRE_FALSE(h.series(s).at(0).value.has_value());
    }
}

TEST_CASE("gapBefore inserts a gap", "[history]") {
    HistoryStore h;
    h.ingest(frameAt(0, 10), false);
    h.ingest(frameAt(3600, 20), true);
    const auto& r = h.series(Series::CpuTotal);
    REQUIRE(r.size() == 3);
    REQUIRE_FALSE(r.at(1).value.has_value());
    REQUIRE(r.at(2).value == 20.0);
}

TEST_CASE("logical cpus grow lazily", "[history]") {
    HistoryStore h;
    Frame a = frameAt(0, 1);
    a.cpu.logical.resize(2);
    a.cpu.logical[1].id = 1;
    a.cpu.logical[0].util = Metric::measured(50, Unit::Percent);
    h.ingest(a, false);
    REQUIRE(h.logicalCount() == 2);
    REQUIRE(h.logicalCpu(0).at(0).value == 50.0);
    REQUIRE_FALSE(h.logicalCpu(1).at(0).value.has_value());

    Frame b = frameAt(1, 1);
    b.cpu.logical.resize(4);
    for (int i = 0; i < 4; ++i) b.cpu.logical[static_cast<std::size_t>(i)].id = i;
    h.ingest(b, false);
    REQUIRE(h.logicalCount() == 4);
    REQUIRE_THROWS_AS(h.logicalCpu(9), std::out_of_range);
}

namespace {
Frame coresAt(int sec, std::initializer_list<int> ids, double util) {
    Frame f = frameAt(sec, 1);
    for (const int id : ids) {
        LogicalCpu l;
        l.id = id;
        l.util = Metric::measured(util + id, Unit::Percent);
        f.cpu.logical.push_back(l);
    }
    return f;
}
}  // namespace

TEST_CASE("a core that goes offline keeps its own history and the others stay put", "[history][minor]") {
    HistoryStore h;
    h.ingest(coresAt(0, {0, 1, 2}, 10), false);
    h.ingest(coresAt(1, {0, 2}, 20), false);  // cpu1 went offline: /proc/stat no longer lists it
    const auto* c1 = h.logicalCpuById(1);
    const auto* c2 = h.logicalCpuById(2);
    REQUIRE(c1 != nullptr);
    REQUIRE(c2 != nullptr);
    REQUIRE(c2->at(1).value == 22.0);         // cpu2's second sample is cpu2's, not cpu1's slot shifted down
    REQUIRE_FALSE(c1->at(1).value.has_value());  // cpu1 has a gap while offline (INT-03)
    h.ingest(coresAt(2, {0, 1, 2}, 30), false);
    REQUIRE(h.logicalCpuById(1)->at(2).value == 31.0);
    REQUIRE(h.logicalCpuById(7) == nullptr);
    REQUIRE(h.logicalId(2) == 2);
}

TEST_CASE("a graph's pointer to a core's history survives more cores appearing", "[history][minor]") {
    HistoryStore h;
    h.ingest(coresAt(0, {0, 1}, 10), false);
    const auto* first = h.logicalCpuById(0);
    const auto* freq = &h.logicalFreq(0);
    for (int id = 2; id < 200; ++id) h.ingest(coresAt(id, {0, 1, id}, 10), false);  // far past any vector growth step
    REQUIRE(h.logicalCpuById(0) == first);
    REQUIRE(&h.logicalFreq(0) == freq);
}

TEST_CASE("capacity limits history", "[history]") {
    HistoryStore h(3);
    for (int i = 0; i < 4; ++i) h.ingest(frameAt(i, i), false);
    REQUIRE(h.series(Series::CpuTotal).size() == 3);
    REQUIRE_THROWS_AS(HistoryStore(0), std::invalid_argument);
}

TEST_CASE("power and hotspot series", "[history]") {
    HistoryStore h;
    Frame f = frameAt(0, 1);
    f.energy.watts = Metric::measured(9.753, Unit::Watts);
    f.thermals.hotspot = Metric::measured(48, Unit::Celsius);
    h.ingest(f, false);
    REQUIRE(h.series(Series::PowerW).at(0).value == 9.753);
    REQUIRE(h.series(Series::HotspotC).at(0).value == 48.0);
    REQUIRE(h.series(Series::HotspotC).at(0).value == 48.0);
}

TEST_CASE("gpu rings grow and keep their references", "[history]") {
    HistoryStore h;
    Frame a = frameAt(0, 1);
    a.gpus.resize(1);
    a.gpus[0].util = Metric::measured(30, Unit::Percent);
    h.ingest(a, false);
    const auto* first = &h.gpuUtil(0);
    REQUIRE(h.gpuUtil(0).at(0).value == 30.0);
    Frame b = frameAt(1, 1);
    b.gpus.resize(3);
    h.ingest(b, false);
    REQUIRE(&h.gpuUtil(0) == first);
    REQUIRE(h.gpuCount() == 3);
    REQUIRE_THROWS_AS(h.gpuUtil(7), std::out_of_range);
    REQUIRE(h.npuCount() == 0);
}

TEST_CASE("sensor rings by id", "[history]") {
    HistoryStore h;
    Frame f = frameAt(0, 1);
    f.thermals.sensors = {{"hwmon:coretemp:temp1", "Package", Metric::measured(48, Unit::Celsius)}};
    h.ingest(f, false);
    REQUIRE(h.sensor("hwmon:coretemp:temp1").at(0).value == 48.0);
    REQUIRE_THROWS_AS(h.sensor("nope"), std::out_of_range);
}

TEST_CASE("history keeps per-core frequency and per-domain power", "[history][power]") {
    pmp::HistoryStore h(10);
    pmp::MonoTime t{};
    for (int i = 0; i < 3; ++i) {
        pmp::Frame f;
        f.mono = t + std::chrono::seconds(i);
        pmp::LogicalCpu c;
        c.freqHz = pmp::Metric::measured(1e9 * (i + 1), pmp::Unit::Hertz);
        f.cpu.logical.push_back(c);
        pmp::PowerDomain d;
        d.id = "package-0";
        d.watts = i == 1 ? pmp::Metric::notMeasured(pmp::Availability::Pending, pmp::Unit::Watts)
                         : pmp::Metric::measured(5.0 + i, pmp::Unit::Watts);
        f.energy.domains.push_back(d);
        h.ingest(f, false);
    }
    REQUIRE(h.logicalFreq(0).size() == 3);
    REQUIRE_THROWS_AS(h.logicalFreq(1), std::out_of_range);
    REQUIRE(h.domainIds() == std::vector<std::string>{"package-0"});
    const auto& r = h.domain("package-0");
    REQUIRE(r.size() == 3);
    REQUIRE_THROWS_AS(h.domain("nope"), std::out_of_range);
    // the Pending sample is a gap, not a zero
    int gaps = 0;
    for (std::size_t i = 0; i < r.size(); ++i) gaps += r.at(i).value.has_value() ? 0 : 1;
    REQUIRE(gaps == 1);
}

TEST_CASE("reset empties every ring without moving it", "[history]") {
    pmp::HistoryStore h(10);
    pmp::Frame f;
    f.mono = pmp::MonoTime{} + std::chrono::seconds(1);
    f.cpu.total = pmp::Metric::measured(5, pmp::Unit::Percent);
    pmp::LogicalCpu c;
    c.util = pmp::Metric::measured(1, pmp::Unit::Percent);
    f.cpu.logical.push_back(c);
    pmp::PowerDomain d;
    d.id = "package-0";
    d.watts = pmp::Metric::measured(2, pmp::Unit::Watts);
    f.energy.domains.push_back(d);
    h.ingest(f, false);
    const auto* total = &h.series(pmp::Series::CpuTotal);
    const auto* logical = &h.logicalCpu(0);
    const auto* dom = &h.domain("package-0");
    REQUIRE(total->size() == 1);
    h.reset();
    // the rings are the same objects (graphs hold pointers to them), now empty
    REQUIRE(&h.series(pmp::Series::CpuTotal) == total);
    REQUIRE(&h.logicalCpu(0) == logical);
    REQUIRE(&h.domain("package-0") == dom);
    REQUIRE(total->size() == 0);
    REQUIRE(logical->size() == 0);
    REQUIRE(dom->size() == 0);
    h.ingest(f, false);
    REQUIRE(total->size() == 1);
}

TEST_CASE("a sensor that vanishes and returns leaves a gap, not a line across the hole", "[history][minor]") {
    pmp::HistoryStore h(20);
    auto frame = [](int sec, bool withSensor) {
        pmp::Frame f;
        f.mono = pmp::MonoTime{} + std::chrono::seconds(sec);
        f.cpu.total = pmp::Metric::measured(1, pmp::Unit::Percent);
        pmp::SensorReading s;
        s.id = "hwmon0/temp1";
        s.celsius = pmp::Metric::measured(50.0 + sec, pmp::Unit::Celsius);
        if (withSensor) f.thermals.sensors.push_back(s);
        pmp::PowerDomain d;
        d.id = "package-0";
        d.watts = pmp::Metric::measured(5, pmp::Unit::Watts);
        if (withSensor) f.energy.domains.push_back(d);
        return f;
    };
    h.ingest(frame(0, true), false);
    h.ingest(frame(1, false), false);  // the sensor (and the power domain) are not in this frame
    h.ingest(frame(2, true), false);
    for (const auto* ring : {&h.sensor("hwmon0/temp1"), &h.domain("package-0")}) {
        REQUIRE(ring->size() == 3);
        REQUIRE(ring->at(0).value.has_value());
        REQUIRE_FALSE(ring->at(1).value.has_value());  // the hole is a gap
        REQUIRE(ring->at(2).value.has_value());
    }
}
