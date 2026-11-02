// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include "frame_json.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using pmp::probe::metricToJson;
using pmp::probe::toJson;

namespace {

ProcessRecord proc(int pid, Metric cpu, bool tombstone = false) {
    ProcessRecord r;
    r.key = {pid, 1000u + static_cast<unsigned>(pid)};
    r.name = "p" + std::to_string(pid);
    r.cpu = cpu;
    r.memFootprint = Metric::measured(4096, Unit::Bytes);
    if (tombstone) r.exitedAt = MonoTime{};
    return r;
}

}  // namespace

TEST_CASE("a measured zero serialises as 0, not null", "[json]") {
    const auto j = metricToJson(Metric::measured(0.0, Unit::BytesPerSec));
    REQUIRE(j["value"] == 0);
    REQUIRE(j["availability"] == "measured");
    REQUIRE(j["unit"] == "bytes_per_sec");
}

TEST_CASE("an unsupported metric serialises as null", "[json]") {
    const auto j = metricToJson(Metric::notMeasured(Availability::Unsupported, Unit::Watts));
    REQUIRE(j["value"].is_null());
    REQUIRE(j["availability"] == "unsupported");
}

TEST_CASE("top processes are ordered by cpu, pending last, limited to topN", "[json]") {
    Frame f;
    f.processes.processes = {proc(1, Metric::measured(5, Unit::Percent)),
                             proc(2, Metric::notMeasured(Availability::Pending, Unit::Percent)),
                             proc(20, Metric::measured(20, Unit::Percent))};
    const auto j = toJson(f, 2, false);
    const auto& top = j["processes"]["top"];
    REQUIRE(top.size() == 2);
    REQUIRE(top[0]["pid"] == 20);
    REQUIRE(top[1]["pid"] == 1);
}

TEST_CASE("tombstones are excluded from top", "[json]") {
    Frame f;
    f.processes.processes = {proc(7, Metric::measured(99, Unit::Percent), true),
                             proc(8, Metric::measured(1, Unit::Percent))};
    const auto j = toJson(f, 10, false);
    REQUIRE(j["processes"]["top"].size() == 1);
    REQUIRE(j["processes"]["top"][0]["pid"] == 8);
}

TEST_CASE("gap_before and generation are passed through", "[json]") {
    Frame f;
    f.generation = 7;
    REQUIRE(toJson(f, 1, true)["gap_before"] == true);
    REQUIRE(toJson(f, 1, false)["gap_before"] == false);
    REQUIRE(toJson(f, 1, false)["generation"] == 7);
}

TEST_CASE("provider health is exported", "[json]") {
    Frame f;
    ProviderHealth h;
    h.id = "cpu";
    h.state = HealthState::Degraded;
    h.lastError = "boom";
    h.consecutiveFailures = 1;
    f.providerHealth.push_back(h);
    const auto j = toJson(f, 1, false)["provider_health"][0];
    REQUIRE(j["id"] == "cpu");
    REQUIRE(j["state"] == "Degraded");
    REQUIRE(j["last_error"] == "boom");
}

TEST_CASE("invalid UTF-8 in a process name does not break serialisation", "[json]") {
    Frame f;
    auto p = proc(5, Metric::measured(1, Unit::Percent));
    p.name = "bad\xff\xfe name";
    f.processes.processes = {p};
    std::string line;
    REQUIRE_NOTHROW(line = pmp::probe::dumpLine(toJson(f, 5, false)));
    REQUIRE(line.find('\n') == std::string::npos);
    REQUIRE(line.find("\xEF\xBF\xBD") != std::string::npos);  // U+FFFD replacement character
}

TEST_CASE("cpu frequency and policy facts are in the json", "[json]") {
    Frame f;
    LogicalCpu c;
    c.freqHz = Metric::measured(2.4e9, Unit::Hertz);
    c.freqMinHz = Metric::measured(8e8, Unit::Hertz);
    c.freqMaxHz = Metric::notMeasured(Availability::Unsupported, Unit::Hertz);
    f.cpu.logical.push_back(c);
    f.cpu.governor = "powersave";
    f.cpu.throttleEvents = Metric::measured(0.0, Unit::Count);
    const auto j = toJson(f, 5, false);
    REQUIRE(j["cpu"]["governor"] == "powersave");
    REQUIRE(j["cpu"]["throttle_events"]["value"] == 0);
    REQUIRE(j["cpu"]["logical"][0]["freq_hz"]["value"] == 2.4e9);
    REQUIRE(j["cpu"]["logical"][0]["freq_max_hz"]["value"].is_null());
}
