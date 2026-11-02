// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/gpu_provider.hpp>

#include <algorithm>
#include <chrono>
#include <set>

namespace pmp::linux {
namespace {

Metric unsupported(Unit u) { return Metric::notMeasured(Availability::Unsupported, u); }

struct EngineSums {
    std::vector<std::pair<std::string, double>> perEngine;  // percent of one engine's capacity, in fdinfo order
    void add(const std::string& name, double pct) {
        for (auto& [n, v] : perEngine) {
            if (n == name) {
                v += pct;
                return;
            }
        }
        perEngine.emplace_back(name, pct);
    }
    double busiest() const {
        double m = 0;
        for (const auto& [n, v] : perEngine) m = std::max(m, v);
        return std::min(100.0, m);
    }
};

std::set<std::string> listDir(const std::filesystem::path& dir) {
    std::set<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) out.insert(e.path().filename().string());
    return out;
}

Metric amdFrequency(const std::filesystem::path& file) {
    const auto text = readText(file).text;  // lines like "1: 1200Mhz *"
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto nl = text.find('\n', pos);
        const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.find('*') == std::string::npos) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        try {
            return Metric::measured(std::stod(line.substr(colon + 1)) * 1e6, Unit::Hertz);
        } catch (...) {
        }
    }
    return unsupported(Unit::Hertz);
}

}  // namespace

GpuProvider::GpuProvider(ProcRoot root, std::filesystem::path pciIds, int slices)
    : root_(std::move(root)), pciIds_(std::move(pciIds)), scanner_(root_, slices) {}

void GpuProvider::sample(Frame& out, const SampleContext& ctx) {
    out.gpus.clear();

    std::vector<std::string> cards;
    for (const auto& name : listDir(root_.sys / "class/drm")) {
        if (name.rfind("card", 0) != 0 || name.size() < 5) continue;
        if (!std::all_of(name.begin() + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        std::error_code ec;
        if (std::filesystem::exists(root_.sys / "class/drm" / name / "device", ec)) cards.push_back(name);
    }

    auto live = [&] {
        std::vector<ProcessRecord*> v;
        for (auto& r : out.processes.processes) {
            if (!r.exitedAt) v.push_back(&r);
        }
        return v;
    }();

    if (cards.empty()) {
        for (auto* r : live) r->gpu = unsupported(Unit::Percent);
        return;
    }

    std::vector<std::int32_t> livePids;
    std::set<std::int32_t> pidSet, newPids;
    for (auto* r : live) {
        livePids.push_back(r->key.pid);
        pidSet.insert(r->key.pid);
        if (!prevPids_.contains(r->key.pid)) newPids.insert(r->key.pid);
    }
    prevPids_ = pidSet;
    const auto clients = scanner_.scan(livePids, newPids);

    const double dtNs = prevTime_ ? std::chrono::duration<double, std::nano>(ctx.mono - *prevTime_).count() : 0.0;
    const bool haveDelta = dtNs > 0;

    std::map<std::string, EngineSums> perDevice;      // pdev -> engines
    std::map<std::int32_t, EngineSums> perProcess;    // pid -> engines
    std::set<std::int32_t> noDeltaYet;                // processes with a client that has no earlier counter to subtract
    std::map<std::string, double> residentByDevice;
    decltype(prevNs_) nowNs;
    for (const auto& c : clients) {
        if (!c.engineNs.empty()) enginePdevs_.insert(c.pdev);  // this driver publishes engine counters: 0 is a real reading
        residentByDevice[c.pdev] += c.residentBytes;
        for (const auto& [engine, ns] : c.engineNs) {
            const auto key = std::make_tuple(c.pdev, c.clientId, engine);
            nowNs[key] = ns;
            const double cap = c.engineCapacity.count(engine) && c.engineCapacity.at(engine) > 0
                                   ? static_cast<double>(c.engineCapacity.at(engine))
                                   : 1.0;
            double pct = 0;
            const auto prev = prevNs_.find(key);
            if (haveDelta && prev != prevNs_.end() && ns >= prev->second) {
                pct = static_cast<double>(ns - prev->second) / (dtNs * cap) * 100.0;
            } else if (haveDelta && prev == prevNs_.end()) {
                noDeltaYet.insert(c.pid);  // first sight of this client: its rate is unknown, not zero
            }
            perDevice[c.pdev].add(engine, pct);
            perProcess[c.pid].add(engine, pct);
        }
    }
    prevNs_ = std::move(nowNs);
    prevTime_ = ctx.mono;

    // Utilisation here is derived from the process table. Without it, or for a driver that publishes no engine
    // counters, "no client found" says nothing about the GPU, so it must not read as a measured 0 (INT-01).
    const bool processesKnown = out.processes.count.isMeasured();
    const Availability whyNot = processesKnown ? Availability::Unsupported : out.processes.count.availability;

    int index = 0;
    for (const auto& card : cards) {
        const auto dev = root_.sys / "class/drm" / card / "device";
        std::error_code ec;
        GpuSnapshot g;
        g.index = index++;
        g.name = gpuName(root_, card, pciIds_);
        g.pciSlot = std::filesystem::read_symlink(dev, ec).filename().string();
        g.driver = std::filesystem::read_symlink(dev / "driver", ec).filename().string();
        // amdgpu reports device-wide busy time itself (gpu_busy_percent), so the total never depends on seeing every client.
        g.visibleClientsOnly = scanner_.sawUnreadable() && g.driver != "amdgpu";

        const auto devIt = perDevice.find(g.pciSlot);
        if (!haveDelta) {
            g.util = Metric::notMeasured(Availability::Pending, Unit::Percent);
        } else if (devIt != perDevice.end()) {
            for (const auto& [name, v] : devIt->second.perEngine) {
                g.engines.push_back({name, Metric::measured(std::min(100.0, v), Unit::Percent)});
            }
            g.util = Metric::measured(devIt->second.busiest(), Unit::Percent);
        } else if (processesKnown && enginePdevs_.count(g.pciSlot)) {
            g.util = Metric::measured(0.0, Unit::Percent);  // no client is using the GPU
        } else {
            g.util = Metric::notMeasured(whyNot, Unit::Percent);
        }

        const bool amd = g.driver == "amdgpu";
        if (amd) {
            const auto busy = readMetric(dev / "gpu_busy_percent", Unit::Percent);
            if (busy.isMeasured()) g.util = busy;
            g.memUsed = readMetric(dev / "mem_info_vram_used", Unit::Bytes);
            g.memTotal = readMetric(dev / "mem_info_vram_total", Unit::Bytes);
            g.freqHz = amdFrequency(dev / "pp_dpm_sclk");
        } else {
            const auto r = residentByDevice.find(g.pciSlot);
            g.memUsed = r != residentByDevice.end()
                            ? Metric::measured(r->second, Unit::Bytes)
                            : !haveDelta ? Metric::notMeasured(Availability::Pending, Unit::Bytes)
                            : (processesKnown && enginePdevs_.count(g.pciSlot)) ? Metric::measured(0.0, Unit::Bytes)
                                                                                : Metric::notMeasured(whyNot, Unit::Bytes);
            g.memTotal = unsupported(Unit::Bytes);
            g.freqHz = readMetric(root_.sys / "class/drm" / card / "gt_act_freq_mhz", Unit::Hertz, 1e6);
        }

        g.temperature = g.power = unsupported(Unit::Celsius);
        g.power = unsupported(Unit::Watts);
        const auto hwmons = listDir(dev / "hwmon");
        if (!hwmons.empty()) {
            const auto hw = dev / "hwmon" / *hwmons.begin();
            g.temperature = readMetric(hw / "temp1_input", Unit::Celsius, 0.001);
            g.power = readMetric(hw / "power1_average", Unit::Watts, 1e-6);
        }
        out.gpus.push_back(std::move(g));
    }

    for (auto* r : live) {
        const auto it = perProcess.find(r->key.pid);
        if (!haveDelta) r->gpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
        else if (it != perProcess.end() && noDeltaYet.contains(r->key.pid) && it->second.busiest() == 0.0)
            r->gpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
        else if (it != perProcess.end()) r->gpu = Metric::measured(it->second.busiest(), Unit::Percent);
        else if (scanner_.covers(r->key.pid)) r->gpu = Metric::measured(0.0, Unit::Percent);
        else r->gpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
    }
}

void GpuProvider::fillUnavailable(Frame& out, Availability why) const {
    out.gpus.clear();
    for (auto& r : out.processes.processes) r.gpu = Metric::notMeasured(why, Unit::Percent);
}

}  // namespace pmp::linux
