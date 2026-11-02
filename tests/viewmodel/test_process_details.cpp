// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/process_details.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace std::chrono_literals;

namespace {

ProcessRecord record() {
    ProcessRecord r;
    r.key = {4445, 100};
    r.name = "Firefox";
    r.user = "dave";
    r.state = 'S';
    r.cpu = Metric::measured(0.1, Unit::Percent);
    r.cpuTime = Metric::measured(1192, Unit::Seconds);
    r.gpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
    r.memFootprint = Metric::measured(3.35 * 1024 * 1024 * 1024, Unit::Bytes);
    r.memPrivate = Metric::measured(3.64 * 1024 * 1024 * 1024, Unit::Bytes);
    r.pageFaults = Metric::measured(722038, Unit::Count);
    r.threads = Metric::measured(103, Unit::Count);
    r.priority = Metric::measured(0, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
    r.exe = "/usr/lib/firefox/firefox";
    r.started = std::chrono::system_clock::time_point{} + 1700000000s;
    return r;
}

CellContext ctx() {
    CellContext c;
    c.wallNow = std::chrono::system_clock::time_point{} + (1700000000s + 19h + 8min);
    return c;
}

const std::vector<StatItem>* section(const ProcessDetails& d, const std::string& name) {
    for (const auto& [n, items] : d.sections)
        if (n == name) return &items;
    return nullptr;
}

std::string value(const ProcessDetails& d, const std::string& sec, const std::string& caption) {
    const auto* s = section(d, sec);
    if (!s) return "<no section>";
    for (const auto& i : *s)
        if (i.caption == caption) return i.value;
    return "<no item>";
}

}  // namespace

TEST_CASE("details have the five columns of PRC-11", "[process-details]") {
    ProcessRecord parent;
    parent.name = "gnome-shell";
    parent.key = {1, 1};
    linux::Inspection in;
    in.architecture = "x86_64";
    const auto d = processDetails(record(), &parent, &in, ctx());
    std::vector<std::string> names;
    for (const auto& [n, items] : d.sections) names.push_back(n);
    REQUIRE(names == std::vector<std::string>{"IDENTITY", "LIFETIME", "PROCESSOR", "MEMORY", "DISK"});
    REQUIRE(value(d, "IDENTITY", "Parent") == "gnome-shell (1)");
    REQUIRE(value(d, "IDENTITY", "User") == "dave");
    REQUIRE(value(d, "IDENTITY", "Status") == "sleeping");
    REQUIRE(value(d, "IDENTITY", "Architecture") == "x86_64");
    REQUIRE(value(d, "LIFETIME", "Threads") == "103");
    REQUIRE(value(d, "LIFETIME", "Priority") == "0");
    REQUIRE(value(d, "PROCESSOR", "CPU") == "0.1%");
    REQUIRE(value(d, "PROCESSOR", "CPU time") == "0:00:19:52");
    REQUIRE(value(d, "PROCESSOR", "GPU") == "—");
    REQUIRE(value(d, "PROCESSOR", "NPU") == "Unavailable");
    REQUIRE(value(d, "MEMORY", "Footprint") == "3.35 GB");
    REQUIRE(value(d, "MEMORY", "Private") == "3.64 GB");
    REQUIRE(value(d, "MEMORY", "Page faults") == "722,038");
    REQUIRE(value(d, "DISK", "Reading") == "Zero KB/s");
    REQUIRE(value(d, "DISK", "Writing") == "Zero KB/s");
}

TEST_CASE("the title is name and pid and the path is shown", "[process-details]") {
    const auto d = processDetails(record(), nullptr, nullptr, ctx());
    REQUIRE(d.title == "Firefox (4445)");
    REQUIRE(d.path == "/usr/lib/firefox/firefox");
    REQUIRE(value(d, "IDENTITY", "Parent") == "—");
    REQUIRE(value(d, "IDENTITY", "Architecture") == "—");
}

TEST_CASE("path shows Restricted for an unreadable exe", "[process-details]") {
    auto r = record();
    r.exe.clear();
    r.exeRestricted = true;
    REQUIRE(processDetails(r, nullptr, nullptr, ctx()).path == "Restricted");
    r.exeRestricted = false;
    REQUIRE(processDetails(r, nullptr, nullptr, ctx()).path == "Unavailable");
}

TEST_CASE("restricted fields are not zeros", "[process-details]") {
    auto r = record();
    r.diskReadBps = Metric::notMeasured(Availability::Restricted, Unit::BytesPerSec);
    r.diskWriteBps = Metric::notMeasured(Availability::Restricted, Unit::BytesPerSec);
    const auto d = processDetails(r, nullptr, nullptr, ctx());
    REQUIRE(value(d, "DISK", "Reading") == "Restricted");
    REQUIRE(value(d, "DISK", "Writing") == "Restricted");
}

TEST_CASE("uptime is derived from the start time", "[process-details]") {
    const auto d = processDetails(record(), nullptr, nullptr, ctx());
    REQUIRE(value(d, "LIFETIME", "Uptime") == "0:19:08:00");
    auto r = record();
    r.started.reset();
    REQUIRE(value(processDetails(r, nullptr, nullptr, ctx()), "LIFETIME", "Uptime") == "—");
}

TEST_CASE("open files and sockets", "[process-details]") {
    linux::Inspection in;
    in.openFiles = 38;
    in.sockets = 5;
    REQUIRE(processDetails(record(), nullptr, &in, ctx()).openFiles == "38 open files, 5 sockets");
    in.openFiles.reset();
    REQUIRE(processDetails(record(), nullptr, &in, ctx()).openFiles == "Restricted");
    REQUIRE(processDetails(record(), nullptr, nullptr, ctx()).openFiles == "—");
}

TEST_CASE("a tombstone explains that it has exited", "[process-details]") {
    auto r = record();
    r.exitedAt = MonoTime{};
    const auto d = processDetails(r, nullptr, nullptr, ctx());
    REQUIRE(d.exitText == "This process has exited. Linux does not report another process's exit status.");
    REQUIRE(value(d, "IDENTITY", "Status") == "exited");
    REQUIRE(processDetails(record(), nullptr, nullptr, ctx()).exitText.empty());
}

TEST_CASE("culprit rows format cpu seconds and memory", "[process-details]") {
    ProcessHistory::Culprit a;
    a.key = {1, 1};
    a.name = "cc1plus";
    a.exited = true;
    a.cpuSeconds = 42.0;
    a.avgCpuPct = 28.0;
    a.peakMemBytes = 1.2 * 1024 * 1024 * 1024;
    a.avgMemBytes = 900.0 * 1024 * 1024;
    const auto cpu = culpritRows({a}, ProcessHistory::Resource::Cpu);
    REQUIRE(cpu[0].name == "cc1plus");
    REQUIRE(cpu[0].primary == "42.0 CPU-seconds");
    REQUIRE(cpu[0].secondary == "avg 28.0%");
    REQUIRE(cpu[0].exited);
    const auto mem = culpritRows({a}, ProcessHistory::Resource::Memory);
    REQUIRE(mem[0].primary == "peak 1.20 GB");
    REQUIRE(mem[0].secondary == "avg 900.0 MB");
}

TEST_CASE("a name with newlines and markup is kept as data", "[process-details]") {
    auto r = record();
    r.name = "<a href=x>evil</a>\nline2";
    REQUIRE(processDetails(r, nullptr, nullptr, ctx()).title == "<a href=x>evil</a>\nline2 (4445)");
}

TEST_CASE("row copy text is tab separated", "[process-details]") {
    REQUIRE(rowCopyText({"Firefox", "4445", "1.0%"}) == "Firefox\t4445\t1.0%");
    REQUIRE(rowCopyText({}).empty());
}
