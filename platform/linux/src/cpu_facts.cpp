// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/cpu_facts.hpp>

#include <charconv>
#include <set>
#include <sstream>
#include <utility>

namespace pmp::linux {
namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    return s;
}

std::string readTrimmed(const std::filesystem::path& p) { return trimmed(readText(p).text); }

Metric unsupported(Unit u) { return Metric::notMeasured(Availability::Unsupported, u); }

Metric khzToHz(const std::filesystem::path& p) {
    const auto f = readText(p);
    long long khz = 0;
    if (!f.ok() || std::from_chars(f.text.data(), f.text.data() + f.text.size(), khz).ec != std::errc{}) {
        return unsupported(Unit::Hertz);
    }
    return Metric::measured(static_cast<double>(khz) * 1000.0, Unit::Hertz);
}

int toInt(const std::string& s, int fallback = -1) {
    int v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

bool hasToken(const std::string& flags, std::string_view token) {
    std::istringstream in(flags);
    std::string t;
    while (in >> t) {
        if (t == token) return true;
    }
    return false;
}

}  // namespace

std::optional<double> parseCacheSize(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.remove_suffix(1);
    double v = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
    if (r.ec != std::errc{}) return std::nullopt;
    const std::string_view unit = text.substr(static_cast<std::size_t>(r.ptr - text.data()));
    if (unit.empty()) return v;
    if (unit == "K") return v * 1024.0;
    if (unit == "M") return v * 1024.0 * 1024.0;
    if (unit == "G") return v * 1024.0 * 1024.0 * 1024.0;
    return std::nullopt;
}

CpuFacts collectCpuFacts(const ProcRoot& root) {
    CpuFacts f;
    f.l1d = f.l1i = f.l2 = f.l3 = unsupported(Unit::Bytes);
    f.baseHz = f.maxHz = unsupported(Unit::Hertz);

    const auto info = readText(root.proc / "cpuinfo");
    if (info.ok()) {
        std::istringstream in(info.text);
        std::string line, flags;
        while (std::getline(in, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = trimmed(line.substr(0, colon));
            if (key == "model name" && f.model.empty()) f.model = trimmed(line.substr(colon + 1));
            if (key == "flags" && flags.empty()) flags = trimmed(line.substr(colon + 1));
        }
        if (!flags.empty()) {
            f.virtualization = hasToken(flags, "vmx") || hasToken(flags, "svm");
            f.virtualMachine = hasToken(flags, "hypervisor");
        }
    }

    const auto cpuDir = root.sys / "devices/system/cpu";
    std::set<int> packages;
    std::set<std::pair<int, int>> cores;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(cpuDir, ec)) {
        const auto name = e.path().filename().string();
        if (name.size() < 4 || name.rfind("cpu", 0) != 0 || toInt(name.substr(3)) < 0) continue;
        ++f.logical;
        const int pkg = toInt(readTrimmed(e.path() / "topology/physical_package_id"));
        const int core = toInt(readTrimmed(e.path() / "topology/core_id"));
        if (pkg >= 0) packages.insert(pkg);
        if (pkg >= 0 && core >= 0) cores.insert({pkg, core});
    }
    f.sockets = static_cast<int>(packages.size());
    f.physicalCores = static_cast<int>(cores.size());

    const auto cpu0 = cpuDir / "cpu0";
    for (const auto& e : std::filesystem::directory_iterator(cpu0 / "cache", ec)) {
        if (e.path().filename().string().rfind("index", 0) != 0) continue;
        const int level = toInt(readTrimmed(e.path() / "level"));
        const auto type = readTrimmed(e.path() / "type");
        const auto size = parseCacheSize(readTrimmed(e.path() / "size"));
        if (!size) continue;
        const Metric m = Metric::measured(*size, Unit::Bytes);
        if (level == 1 && type == "Data") f.l1d = m;
        else if (level == 1 && type == "Instruction") f.l1i = m;
        else if (level == 2) f.l2 = m;
        else if (level == 3) f.l3 = m;
    }
    f.baseHz = khzToHz(cpu0 / "cpufreq/base_frequency");
    f.maxHz = khzToHz(cpu0 / "cpufreq/cpuinfo_max_freq");
    f.driver = readTrimmed(cpu0 / "cpufreq/scaling_driver");
    f.governor = readTrimmed(cpu0 / "cpufreq/scaling_governor");
    f.epp = readTrimmed(cpu0 / "cpufreq/energy_performance_preference");
    return f;
}

}  // namespace pmp::linux
