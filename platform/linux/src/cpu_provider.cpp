// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/cpu_provider.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <set>

namespace pmp::linux {
namespace {

constexpr int kAggregate = -1;

struct StatLine {
    int id;
    std::uint64_t busy, idle, kernel;
};

std::optional<StatLine> parseStatLine(std::string_view line) {
    if (!line.starts_with("cpu")) return std::nullopt;
    const auto sp = line.find(' ');
    if (sp == std::string_view::npos) return std::nullopt;
    StatLine out{kAggregate, 0, 0, 0};
    if (sp > 3) {
        const auto idStr = line.substr(3, sp - 3);
        if (std::from_chars(idStr.data(), idStr.data() + idStr.size(), out.id).ec != std::errc{}) {
            return std::nullopt;
        }
    }
    std::uint64_t f[8] = {};  // user nice system idle iowait irq softirq steal
    std::size_t n = 0;
    std::string_view rest = line.substr(sp);
    while (n < 8) {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.empty()) break;
        const auto r = std::from_chars(rest.data(), rest.data() + rest.size(), f[n]);
        if (r.ec != std::errc{}) return std::nullopt;
        rest.remove_prefix(static_cast<std::size_t>(r.ptr - rest.data()));
        ++n;
    }
    if (n < 4) return std::nullopt;
    out.busy = f[0] + f[1] + f[2] + f[5] + f[6] + f[7];
    out.idle = f[3] + f[4];
    out.kernel = f[2] + f[5] + f[6];
    return out;
}

}  // namespace

void CpuProvider::classifyCores(const std::vector<int>& ids) {
    classified_ = true;
    for (int id : ids) classes_[id] = CoreClass::Unknown;

    const auto core = readText(root_.sys / "devices/cpu_core/cpus");
    const auto atom = readText(root_.sys / "devices/cpu_atom/cpus");
    if (core.ok() && atom.ok()) {
        for (int id : parseCpuList(core.text)) classes_[id] = CoreClass::Performance;
        for (int id : parseCpuList(atom.text)) classes_[id] = CoreClass::Efficiency;
        return;
    }

    std::map<int, long> caps;
    for (int id : ids) {
        const auto f = readText(root_.sys / "devices/system/cpu" / ("cpu" + std::to_string(id)) / "cpu_capacity");
        long v = 0;
        if (f.ok() && std::from_chars(f.text.data(), f.text.data() + f.text.size(), v).ec == std::errc{}) {
            caps[id] = v;
        }
    }
    if (caps.size() != ids.size() || caps.empty()) return;
    const auto [lo, hi] = std::minmax_element(caps.begin(), caps.end(),
                                              [](auto& a, auto& b) { return a.second < b.second; });
    if (lo->second == hi->second) return;
    for (auto& [id, cap] : caps) {
        classes_[id] = cap == hi->second ? CoreClass::Performance : CoreClass::Efficiency;
    }
}

void CpuProvider::sample(Frame& out, const SampleContext& ctx) {
    const auto stat = readText(root_.proc / "stat");
    if (!stat.ok()) {
        fillUnavailable(out, availabilityForErrno(stat.error));
        return;
    }

    std::vector<StatLine> lines;
    std::string_view text = stat.text;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        if (auto l = parseStatLine(text.substr(0, nl))) lines.push_back(*l);
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }

    if (!classified_) {
        std::vector<int> ids;
        for (const auto& l : lines) {
            if (l.id != kAggregate) ids.push_back(l.id);
        }
        classifyCores(ids);
    }

    std::map<int, Counters> now;
    out.cpu.logical.clear();
    for (const auto& l : lines) {
        Metric util = Metric::notMeasured(Availability::Pending, Unit::Percent);
        Metric kernel = util;
        const auto it = prev_.find(l.id);
        if (it != prev_.end() && l.busy >= it->second.busy && l.idle >= it->second.idle &&
            l.kernel >= it->second.kernel) {
            const double dBusy = static_cast<double>(l.busy - it->second.busy);
            const double dIdle = static_cast<double>(l.idle - it->second.idle);
            const double dKernel = static_cast<double>(l.kernel - it->second.kernel);
            if (dBusy + dIdle > 0) {
                util = Metric::measured(dBusy / (dBusy + dIdle) * 100.0, Unit::Percent);
                kernel = Metric::measured(dKernel / (dBusy + dIdle) * 100.0, Unit::Percent);
            }
        }
        now[l.id] = {l.busy, l.idle, l.kernel};
        if (l.id == kAggregate) {
            out.cpu.total = util;
            out.cpu.kernel = kernel;
        } else {
            LogicalCpu c;
            c.id = l.id;
            const auto cls = classes_.find(l.id);
            c.coreClass = cls == classes_.end() ? CoreClass::Unknown : cls->second;
            c.util = util;
            c.kernel = kernel;
            out.cpu.logical.push_back(std::move(c));
        }
    }
    prev_ = std::move(now);

    // Per-core frequency (kHz in sysfs); the headline figure is the mean of the cores that report one. Only the current
    // frequency is read every sample: limits, topology, policy and throttle counters change rarely, so they are read
    // on the first sample and then every few generations (the sampler's budget is per process, NFR-02).
    const auto cpuDir = root_.sys / "devices/system/cpu";
    const bool slow = !policyRead_ || ctx.generation % 10 == 0;
    if (slow) {
        limits_.clear();
        coreReps_.clear();
        pkgReps_.clear();
        std::set<std::pair<long long, long long>> cores;
        std::set<long long> packages;
        for (const auto& l : out.cpu.logical) {
            const auto dir = cpuDir / ("cpu" + std::to_string(l.id));
            limits_[l.id] = {readMetric(dir / "cpufreq/cpuinfo_min_freq", Unit::Hertz, 1000.0),
                             readMetric(dir / "cpufreq/cpuinfo_max_freq", Unit::Hertz, 1000.0)};
            const long long pkg = static_cast<long long>(readNumber(dir / "topology/physical_package_id").value.value_or(0));
            const auto coreId = readNumber(dir / "topology/core_id").value;
            const long long core = coreId ? static_cast<long long>(*coreId) : -1 - l.id;  // unknown topology: one core per cpu
            if (cores.insert({pkg, core}).second) coreReps_.push_back(l.id);  // one cpu speaks for its hyper-threads
            if (packages.insert(pkg).second) pkgReps_.push_back(l.id);
        }
    }
    double freqSum = 0;
    int freqCount = 0;
    for (auto& l : out.cpu.logical) {
        l.freqHz = readMetric(cpuDir / ("cpu" + std::to_string(l.id)) / "cpufreq/scaling_cur_freq", Unit::Hertz, 1000.0);
        if (const auto it = limits_.find(l.id); it != limits_.end()) {
            l.freqMinHz = it->second.first;
            l.freqMaxHz = it->second.second;
        } else {
            l.freqMinHz = l.freqMaxHz = Metric::notMeasured(Availability::Unsupported, Unit::Hertz);
        }
        if (l.freqHz.isMeasured()) {
            freqSum += *l.freqHz.value;
            ++freqCount;
        }
    }
    out.cpu.freqHz = freqCount > 0 ? Metric::measured(freqSum / freqCount, Unit::Hertz)
                                   : Metric::notMeasured(Availability::Unsupported, Unit::Hertz);

    if (slow) {  // driver, governor and energy preference
        const auto first = out.cpu.logical.empty() ? 0 : out.cpu.logical.front().id;
        const auto dir = cpuDir / ("cpu" + std::to_string(first)) / "cpufreq";
        auto trimmed = [](const FileText& t) {
            std::string v = t.ok() ? t.text : std::string();
            while (!v.empty() && (v.back() == '\n' || v.back() == ' ')) v.pop_back();
            return v;
        };
        driver_ = trimmed(readText(dir / "scaling_driver"));
        governor_ = trimmed(readText(dir / "scaling_governor"));
        epp_ = trimmed(readText(dir / "energy_performance_preference"));
        policyRead_ = true;
    }
    out.cpu.driver = driver_;
    out.cpu.governor = governor_;
    out.cpu.epp = epp_;

    // Thermal-throttle events since boot: one per-core counter per physical core plus one per-package counter per
    // package. A cumulative count read every fifth generation is as good as one read every second.
    if (slow || ctx.generation % 5 == 0 || !throttleRead_) {
        double total = 0;
        bool any = false;
        for (const int id : coreReps_) {
            if (const auto n = readNumber(cpuDir / ("cpu" + std::to_string(id)) / "thermal_throttle/core_throttle_count"); n.value) {
                any = true;
                total += *n.value;
            }
        }
        for (const int id : pkgReps_) {
            if (const auto n = readNumber(cpuDir / ("cpu" + std::to_string(id)) / "thermal_throttle/package_throttle_count"); n.value) {
                any = true;
                total += *n.value;
            }
        }
        throttle_ = any ? Metric::measured(total, Unit::Count) : Metric::notMeasured(Availability::Unsupported, Unit::Count);
        throttleRead_ = true;
    }
    out.cpu.throttleEvents = throttle_;

    const auto up = readText(root_.proc / "uptime");
    double secs = 0;
    if (up.ok() && std::from_chars(up.text.data(), up.text.data() + up.text.size(), secs).ec == std::errc{}) {
        out.cpu.uptime = Metric::measured(secs, Unit::Seconds);
    } else {
        out.cpu.uptime = Metric::notMeasured(up.ok() ? Availability::Failed : availabilityForErrno(up.error),
                                             Unit::Seconds);
    }
}

void CpuProvider::fillUnavailable(Frame& out, Availability why) const {
    out.cpu.total = out.cpu.kernel = Metric::notMeasured(why, Unit::Percent);
    out.cpu.uptime = Metric::notMeasured(why, Unit::Seconds);
    out.cpu.freqHz = Metric::notMeasured(why, Unit::Hertz);
    out.cpu.throttleEvents = Metric::notMeasured(why, Unit::Count);
    out.cpu.driver.clear();
    out.cpu.governor.clear();
    out.cpu.epp.clear();
    out.cpu.logical.clear();
}

}  // namespace pmp::linux
