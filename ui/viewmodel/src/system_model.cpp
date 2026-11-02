// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/system_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <cmath>
#include <ctime>
#include <format>
#include <iomanip>
#include <sstream>

namespace pmp::vm {
namespace {

std::string orUnavailable(const std::string& s) { return s.empty() ? "Unavailable" : s; }

// "48 KB", "12 MB", "1.25 MB": exact sizes read naturally, unlike the one-decimal meter format.
std::string cacheText(const Metric& m) {
    if (!m.isMeasured()) return "Unavailable";
    const double b = *m.value;
    constexpr double MiB = 1024.0 * 1024.0;
    if (b >= MiB) return std::format("{:g} MB", b / MiB);
    return std::format("{:g} KB", b / 1024.0);
}

std::string yesNo(const std::optional<bool>& v, const char* yes, const char* no) {
    return v ? (*v ? yes : no) : "Unavailable";
}

std::string countText(int n) { return n > 0 ? std::to_string(n) : "Unavailable"; }

std::string bootTimeText(const Metric& m) {
    if (!m.isMeasured()) return format(m);
    const std::time_t t = static_cast<std::time_t>(*m.value);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

}  // namespace

std::vector<StatItem> cpuFactItems(const linux::CpuFacts& c) {
    const std::string l1 = c.l1d.isMeasured() && c.l1i.isMeasured() ? cacheText(c.l1d) + " + " + cacheText(c.l1i)
                           : c.l1d.isMeasured()                      ? cacheText(c.l1d)
                                                                     : "Unavailable";
    return {{"Base speed", c.baseHz.isMeasured() ? format(c.baseHz) : "Unavailable"},
            {"Sockets", countText(c.sockets)},
            {"Physical cores", countText(c.physicalCores)},
            {"Logical processors", countText(c.logical)},
            {"Virtualization", yesNo(c.virtualization, "Supported", "Not supported")},
            {"Virtual machine", yesNo(c.virtualMachine, "Yes", "No")},
            {"L1 cache", l1},
            {"L2 cache", cacheText(c.l2)},
            {"L3 cache", cacheText(c.l3)},
            {"Frequency driver", orUnavailable(c.driver)},
            {"Frequency governor", orUnavailable(c.governor)},
            {"Power preference", orUnavailable(c.epp)}};
}

std::vector<InfoSection> systemInfoSections(const linux::SystemInfo& s, bool includeIdentifiers) {
    std::vector<InfoSection> out;

    InfoSection overview{"Overview", {}};
    overview.rows = {{"Device name", orUnavailable(s.hostname), false},
                     {"Manufacturer", orUnavailable(s.vendor), false},
                     {"Model", orUnavailable(s.model), false},
                     {"Operating system", orUnavailable(s.osName), false},
                     {"Kernel", orUnavailable(s.kernel), false},
                     {"Architecture", orUnavailable(s.architecture), false},
                     {"Up time", format(s.uptime), false},
                     {"Boot time", bootTimeText(s.bootTime), false},
                     {"Firmware version", orUnavailable(s.biosVersion), false},
                     {"Secure Boot", yesNo(s.secureBoot, "Enabled", "Disabled"), false},
                     {"Serial number", orUnavailable(s.serial), true}};
    out.push_back(std::move(overview));

    InfoSection cpu{"Processor", {{"Model", orUnavailable(s.cpu.model), false}}};
    for (const auto& i : cpuFactItems(s.cpu)) cpu.rows.push_back({i.caption, i.value, false});
    out.push_back(std::move(cpu));

    out.push_back({"Memory", {{"Installed", format(s.memTotal), false}, {"Swap", format(s.swapTotal), false}}});

    InfoSection gfx{"Graphics", {}};
    for (std::size_t i = 0; i < s.gpus.size(); ++i) gfx.rows.push_back({"GPU " + std::to_string(i), s.gpus[i], false});
    if (gfx.rows.empty()) gfx.rows.push_back({"GPU", "Unavailable", false});
    out.push_back(std::move(gfx));

    InfoSection net{"Network adapters", {}};
    for (const auto& a : s.adapters) {
        std::string line = a.isVirtual ? "virtual" : "physical";
        if (!a.ipv4.empty()) line += ", " + a.ipv4;
        line += ", " + format(a.speedBps);
        net.rows.push_back({a.name, line, false});
        net.rows.push_back({a.name + " MAC", orUnavailable(a.mac), true});
    }
    if (net.rows.empty()) net.rows.push_back({"Adapters", "Unavailable", false});
    out.push_back(std::move(net));

    if (!includeIdentifiers) {
        for (auto& sec : out) {
            std::erase_if(sec.rows, [](const InfoRow& r) { return r.identifier; });
        }
    }
    return out;
}

std::string exportText(const std::vector<InfoSection>& sections) {
    std::string out;
    for (const auto& s : sections) {
        out += "[" + s.title + "]\n";
        for (const auto& r : s.rows) out += r.key + ": " + r.value + "\n";
        out += "\n";
    }
    return out;
}

}  // namespace pmp::vm
