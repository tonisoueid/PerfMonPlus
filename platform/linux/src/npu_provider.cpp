// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/npu_provider.hpp>

#include <algorithm>
#include <chrono>
#include <set>

namespace pmp::linux {
namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string vendorName(std::string v) {
    v = trimmed(v);
    if (v.rfind("0x", 0) == 0) v = v.substr(2);
    if (v == "8086") return "Intel";
    if (v == "1002") return "AMD";
    if (v == "1022") return "AMD";
    return "Unknown";
}

}  // namespace

void NpuProvider::sample(Frame& out, const SampleContext& ctx) {
    out.npus.clear();
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(root_.sys / "class/accel", ec)) {
        names.insert(e.path().filename().string());
    }
    const double dtUs = prevTime_ ? std::chrono::duration<double, std::micro>(ctx.mono - *prevTime_).count() : 0.0;
    std::map<std::string, double> now;
    int index = 0;
    for (const auto& name : names) {
        const auto dev = root_.sys / "class/accel" / name / "device";
        NpuSnapshot n;
        n.index = index++;
        const auto driver = std::filesystem::read_symlink(dev / "driver", ec).filename().string();
        n.name = vendorName(readText(dev / "vendor").text) + " NPU" + (driver.empty() ? "" : " (" + driver + ")");
        const auto busy = readNumber(dev / "npu_busy_time_us");
        if (!busy.value) {
            n.activePct = Metric::notMeasured(availabilityForErrno(busy.error), Unit::Percent);
        } else {
            now[name] = *busy.value;
            const auto prev = prevBusyUs_.find(name);
            if (prev != prevBusyUs_.end() && dtUs > 0 && *busy.value >= prev->second) {
                n.activePct = Metric::measured(std::clamp((*busy.value - prev->second) / dtUs * 100.0, 0.0, 100.0), Unit::Percent);
            } else {
                n.activePct = Metric::notMeasured(Availability::Pending, Unit::Percent);
            }
        }
        out.npus.push_back(std::move(n));
    }
    prevBusyUs_ = std::move(now);
    prevTime_ = ctx.mono;
}

void NpuProvider::fillUnavailable(Frame& out, Availability) const { out.npus.clear(); }

}  // namespace pmp::linux
