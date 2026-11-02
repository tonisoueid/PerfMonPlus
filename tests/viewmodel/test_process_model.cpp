// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/process_model.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace pmp;
using namespace pmp::vm;
using namespace std::chrono_literals;

namespace {

ProcessRecord record() {
    ProcessRecord r;
    r.key = {4445, 100};
    r.name = "firefox";
    r.user = "dave";
    r.state = 'S';
    r.cpu = Metric::measured(12.34, Unit::Percent);
    r.memFootprint = Metric::measured(1019.5 * 1024 * 1024, Unit::Bytes);
    r.memPrivate = Metric::measured(900.0 * 1024 * 1024, Unit::Bytes);
    r.threads = Metric::measured(103, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(2048, Unit::BytesPerSec);
    r.gpu = Metric::measured(3.0, Unit::Percent);
    r.cpuTime = Metric::measured(5.0, Unit::Seconds);
    r.priority = Metric::measured(-5, Unit::Count);
    r.pageFaults = Metric::measured(722038, Unit::Count);
    r.exe = "/usr/lib/firefox/firefox";
    r.kind = ProcessKind::App;
    return r;
}

CellContext ctx() {
    CellContext c;
    c.totalMemoryBytes = 16.0 * 1024 * 1024 * 1024;
    c.maxCpuPct = 24.68;
    return c;
}

}  // namespace

TEST_CASE("the default columns are exactly the six of PRC-03", "[process-model]") {
    const std::vector<ProcessColumn> expected{ProcessColumn::Name, ProcessColumn::Pid,    ProcessColumn::Status,
                                              ProcessColumn::User, ProcessColumn::Cpu, ProcessColumn::Memory};
    REQUIRE(defaultColumns() == expected);
}

TEST_CASE("column keys are unique and round trip", "[process-model]") {
    std::set<std::string> keys;
    for (int i = 0; i < kColumnCount; ++i) {
        const auto c = static_cast<ProcessColumn>(i);
        REQUIRE(columnInfo(c).id == c);
        REQUIRE(keys.insert(columnInfo(c).key).second);
        REQUIRE(columnFromKey(columnInfo(c).key) == c);
    }
    REQUIRE(columnFromKey("cpu") == ProcessColumn::Cpu);
    REQUIRE_FALSE(columnFromKey("nonsense").has_value());
}

TEST_CASE("status text covers every state letter and tombstones", "[process-model]") {
    REQUIRE(statusText('R', false) == "running");
    REQUIRE(statusText('S', false) == "sleeping");
    REQUIRE(statusText('I', false) == "sleeping");
    REQUIRE(statusText('D', false) == "waiting for I/O");
    REQUIRE(statusText('T', false) == "suspended");
    REQUIRE(statusText('t', false) == "stopped");
    REQUIRE(statusText('Z', false) == "zombie");
    REQUIRE(statusText('X', false) == "dead");
    REQUIRE(statusText('?', false) == "—");
    REQUIRE(statusText('R', true) == "exited");
}

TEST_CASE("a measured zero is not a missing value", "[process-model]") {
    auto r = record();
    r.cpu = Metric::measured(0, Unit::Percent);
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::Cpu)] == "0.0%");
    r.cpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::Cpu)] == "—");
    r.diskReadBps = Metric::notMeasured(Availability::Restricted, Unit::BytesPerSec);
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::DiskRead)] == "Restricted");
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::DiskRead)] == "Zero KB/s");
}

TEST_CASE("restricted fields are not zeros", "[process-model]") {
    auto r = record();
    r.exe.clear();
    r.exeRestricted = true;
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::Path)] == "Restricted");
    r.exeRestricted = false;  // a kernel thread: there is simply no executable
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::Path)] == "—");
    r.cmdline.clear();
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::CommandLine)] == "—");
}

TEST_CASE("the common columns format as in the reference", "[process-model]") {
    const auto c = processCells(record(), ctx());
    const auto at = [&](ProcessColumn col) { return c[static_cast<int>(col)]; };
    REQUIRE(at(ProcessColumn::Name) == "firefox");
    REQUIRE(at(ProcessColumn::Pid) == "4445");
    REQUIRE(at(ProcessColumn::Status) == "sleeping");
    REQUIRE(at(ProcessColumn::User) == "dave");
    REQUIRE(at(ProcessColumn::Cpu) == "12.3%");
    REQUIRE(at(ProcessColumn::Memory) == "1,019.5 MB");
    REQUIRE(at(ProcessColumn::PrivateMemory) == "900.0 MB");
    REQUIRE(at(ProcessColumn::Threads) == "103");
    REQUIRE(at(ProcessColumn::DiskWrite) == "2 KB/s");
    REQUIRE(at(ProcessColumn::Gpu) == "3.0%");
    REQUIRE(at(ProcessColumn::CpuTime) == "0:00:00:05");
    REQUIRE(at(ProcessColumn::Priority) == "-5");
    REQUIRE(at(ProcessColumn::PageFaults) == "722,038");
    REQUIRE(at(ProcessColumn::Path) == "/usr/lib/firefox/firefox");
    REQUIRE(at(ProcessColumn::Kind) == "App");
}

TEST_CASE("start time is local wall time or a dash", "[process-model]") {
    auto r = record();
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::StartTime)] == "—");
    r.started = std::chrono::system_clock::time_point{} + 1700000000s;
    const auto s = processCells(r, ctx())[static_cast<int>(ProcessColumn::StartTime)];
    REQUIRE(s.size() == 19);  // "YYYY-MM-DD HH:MM:SS"
    REQUIRE(s.substr(0, 2) == "20");
    REQUIRE(s[4] == '-');
    REQUIRE(s[13] == ':');
}

TEST_CASE("network and npu are unavailable and say why", "[process-model]") {
    const auto c = processCells(record(), ctx());
    REQUIRE(c[static_cast<int>(ProcessColumn::NetRx)] == "Unavailable");
    REQUIRE(c[static_cast<int>(ProcessColumn::NetTx)] == "Unavailable");
    REQUIRE(c[static_cast<int>(ProcessColumn::Npu)] == "Unavailable");
    REQUIRE(std::string(columnInfo(ProcessColumn::NetRx).tooltip).find("not available") != std::string::npos);
}

TEST_CASE("energy is a relative estimate and is labelled as one", "[process-model]") {
    auto r = record();
    r.cpu = Metric::measured(12.34, Unit::Percent);
    CellContext c = ctx();
    c.maxCpuPct = 24.68;
    REQUIRE(processCells(r, c)[static_cast<int>(ProcessColumn::Energy)] == "50");
    c.maxCpuPct = 0;
    r.cpu = Metric::measured(0, Unit::Percent);
    REQUIRE(processCells(r, c)[static_cast<int>(ProcessColumn::Energy)] == "0");
    r.cpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
    REQUIRE(processCells(r, c)[static_cast<int>(ProcessColumn::Energy)] == "—");
    REQUIRE(std::string(columnInfo(ProcessColumn::Energy).tooltip).find("Relative demand from current CPU use") == 0);
}

TEST_CASE("a name with markup is data", "[process-model]") {
    auto r = record();
    r.name = "<b>x</b>\n&";
    REQUIRE(processCells(r, ctx())[static_cast<int>(ProcessColumn::Name)] == "<b>x</b>\n&");
}

TEST_CASE("kind text", "[process-model]") {
    REQUIRE(kindText(ProcessKind::App) == "App");
    REQUIRE(kindText(ProcessKind::Background) == "Background");
    REQUIRE(kindText(ProcessKind::System) == "System");
}
