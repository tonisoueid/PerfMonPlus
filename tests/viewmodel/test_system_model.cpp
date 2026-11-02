// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/system_model.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;

namespace {

linux::SystemInfo sample() {
    linux::SystemInfo s;
    s.hostname = "framework";
    s.osName = "Ubuntu 26.04.1 LTS";
    s.kernel = "7.0.0-38-generic";
    s.vendor = "LG Electronics";
    s.model = "17Z90P-G.AA75F";
    s.serial = "ABC123";
    s.uptime = Metric::measured(485479, Unit::Seconds);
    s.memTotal = Metric::measured(16.0 * 1024 * 1024 * 1024, Unit::Bytes);
    s.swapTotal = Metric::measured(4.0 * 1024 * 1024 * 1024, Unit::Bytes);
    s.cpu.model = "11th Gen Intel(R) Core(TM) i7-1165G7";
    s.cpu.l3 = Metric::measured(12288.0 * 1024, Unit::Bytes);
    s.cpu.l2 = Metric::measured(1280.0 * 1024, Unit::Bytes);
    s.cpu.l1d = Metric::measured(48.0 * 1024, Unit::Bytes);
    s.cpu.l1i = Metric::measured(32.0 * 1024, Unit::Bytes);
    s.cpu.baseHz = Metric::measured(2.8e9, Unit::Hertz);
    s.cpu.sockets = 1;
    s.cpu.physicalCores = 4;
    s.cpu.logical = 8;
    s.cpu.virtualization = true;
    s.cpu.virtualMachine = false;
    s.cpu.driver = "intel_pstate";
    s.gpus = {"TigerLake-LP GT2 [Iris Xe Graphics]"};
    linux::NetAdapter a;
    a.name = "wlp0s20f3";
    a.mac = "8c:b8:7e:d9:0e:c2";
    a.ipv4 = "192.168.1.20";
    a.speedBps = Metric::notMeasured(Availability::Failed, Unit::BytesPerSec);
    s.adapters = {a};
    return s;
}

bool anyValueContains(const std::vector<InfoSection>& sections, const std::string& needle) {
    for (const auto& s : sections)
        for (const auto& r : s.rows)
            if (r.value.find(needle) != std::string::npos || r.key.find(needle) != std::string::npos) return true;
    return false;
}

std::string valueOf(const std::vector<StatItem>& items, const std::string& caption) {
    for (const auto& i : items)
        if (i.caption == caption) return i.value;
    return "<missing>";
}

}  // namespace

TEST_CASE("identifiers are hidden by default", "[system]") {
    const auto hidden = systemInfoSections(sample(), false);
    REQUIRE_FALSE(anyValueContains(hidden, "ABC123"));
    REQUIRE_FALSE(anyValueContains(hidden, "8c:b8:7e:d9:0e:c2"));
    const auto shown = systemInfoSections(sample(), true);
    REQUIRE(anyValueContains(shown, "ABC123"));
    REQUIRE(anyValueContains(shown, "8c:b8:7e:d9:0e:c2"));
}

TEST_CASE("sections cover the machine", "[system]") {
    const auto s = systemInfoSections(sample(), false);
    std::vector<std::string> titles;
    for (const auto& sec : s) titles.push_back(sec.title);
    REQUIRE(titles == std::vector<std::string>{"Overview", "Processor", "Memory", "Graphics", "Network adapters"});
    REQUIRE(anyValueContains(s, "Ubuntu 26.04.1 LTS"));
    REQUIRE(anyValueContains(s, "LG Electronics"));
    REQUIRE(anyValueContains(s, "TigerLake-LP GT2 [Iris Xe Graphics]"));
    REQUIRE(anyValueContains(s, "192.168.1.20"));
}

TEST_CASE("an unreadable link speed is shown as unavailable", "[system]") {
    const auto s = systemInfoSections(sample(), false);
    REQUIRE(anyValueContains(s, "Unavailable"));
}

TEST_CASE("cpu fact items", "[system]") {
    const auto items = cpuFactItems(sample().cpu);
    REQUIRE(valueOf(items, "L3 cache") == "12 MB");
    REQUIRE(valueOf(items, "L2 cache") == "1.25 MB");
    REQUIRE(valueOf(items, "L1 cache") == "48 KB + 32 KB");
    REQUIRE(valueOf(items, "Base speed") == "2.80 GHz");
    REQUIRE(valueOf(items, "Virtualization") == "Supported");
    REQUIRE(valueOf(items, "Virtual machine") == "No");
    REQUIRE(valueOf(items, "Sockets") == "1");
    REQUIRE(valueOf(items, "Physical cores") == "4");
    REQUIRE(valueOf(items, "Logical processors") == "8");
    REQUIRE(valueOf(items, "Frequency driver") == "intel_pstate");
}

TEST_CASE("unknown cpu facts read Unavailable", "[system]") {
    linux::CpuFacts none;
    const auto items = cpuFactItems(none);
    REQUIRE(valueOf(items, "Virtualization") == "Unavailable");
    REQUIRE(valueOf(items, "Virtual machine") == "Unavailable");
    REQUIRE(valueOf(items, "L3 cache") == "Unavailable");
    REQUIRE(valueOf(items, "Sockets") == "Unavailable");
    REQUIRE(valueOf(items, "Frequency driver") == "Unavailable");
}

TEST_CASE("export text format", "[system]") {
    const std::vector<InfoSection> sections = {{"One", {{"A", "1", false}, {"B", "2", false}}}, {"Two", {{"C", "3", false}}}};
    REQUIRE(exportText(sections) == "[One]\nA: 1\nB: 2\n\n[Two]\nC: 3\n\n");
}
