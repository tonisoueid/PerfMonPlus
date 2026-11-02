// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/hardware_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace pmp::vm {

TileText gpuTile(const GpuSnapshot& g) {
    return {"GPU " + std::to_string(g.index), format(g.util), g.name, g.util.isMeasured()};
}

TileText npuTile(const NpuSnapshot& n) {
    return {"NPU " + std::to_string(n.index), format(n.activePct), n.name, n.activePct.isMeasured()};
}

std::string pressureText(ThermalPressure p) {
    switch (p) {
        case ThermalPressure::Nominal: return "Nominal";
        case ThermalPressure::Moderate: return "Moderate";
        case ThermalPressure::Heavy: return "Heavy";
        case ThermalPressure::Critical: return "Critical";
        case ThermalPressure::Unknown: return "Unavailable";
    }
    return "Unavailable";
}

std::string powerSourceText(const EnergySnapshot& e) {
    switch (e.source) {
        case PowerSource::AC: return "AC Power";
        case PowerSource::Battery:
            return e.battery.chargePct.isMeasured()
                       ? "Battery " + std::to_string(static_cast<int>(std::lround(*e.battery.chargePct.value))) + "%"
                       : "Battery";
        case PowerSource::Unknown: return "Unavailable";
    }
    return "Unavailable";
}

TileText energyTile(const Frame& f) {
    std::string pressure = pressureText(f.thermals.pressure);
    for (auto& c : pressure) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return {"Energy", format(f.energy.watts), "Thermals " + pressure + " · " + powerSourceText(f.energy),
            f.energy.watts.isMeasured()};
}

TileText thermalsTile(const Frame& f) {
    return {"Thermals", format(f.thermals.hotspot), pressureText(f.thermals.pressure) + " thermal pressure",
            f.thermals.hotspot.isMeasured()};
}

std::vector<Chip> energyChips(const Frame& f) {
    return {{"Thermal state", pressureText(f.thermals.pressure)},
            {"Power mode", f.energy.powerMode.empty() ? "Unavailable" : f.energy.powerMode},
            {"Power source", powerSourceText(f.energy)}};
}

std::vector<Chip> thermalChips(const Frame& f) {
    return {{"Thermal pressure", pressureText(f.thermals.pressure)}, {"Hotspot", format(f.thermals.hotspot)}};
}

std::vector<StatItem> batteryStats(const EnergySnapshot& e) {
    const auto& b = e.battery;
    if (!b.present) return {};
    return {{"Charge", format(b.chargePct)},
            {b.timeIsToFull ? "Time to full" : "Time remaining", format(b.timeRemaining)},
            {"Health", format(b.healthPct)},
            {"Cycle count", format(b.cycleCount)},
            {"Rate", format(b.rateW)}};
}

std::vector<EnergyRow> energyDemand(const Frame& f, std::size_t n) {
    struct P {
        const ProcessRecord* r;
        double cpu;
    };
    std::vector<P> live;
    double sum = 0;
    for (const auto& r : f.processes.processes) {
        if (r.exitedAt || !r.cpu.isMeasured()) continue;
        live.push_back({&r, *r.cpu.value});
        sum += *r.cpu.value;
    }
    std::stable_sort(live.begin(), live.end(), [](const P& a, const P& b) { return a.cpu > b.cpu; });
    const bool packageKnown = f.energy.wattsSource == WattsSource::CpuPackage && f.energy.watts.isMeasured() && sum > 0;
    std::vector<EnergyRow> rows;
    const double top = live.empty() ? 0 : live.front().cpu;
    for (std::size_t i = 0; i < live.size() && i < n; ++i) {
        EnergyRow row;
        row.key = live[i].r->key;
        row.name = live[i].r->name;
        row.score = top > 0 ? live[i].cpu / top * 100.0 : 0.0;
        if (packageKnown) {
            const double watts = *f.energy.watts.value * live[i].cpu / sum;
            char buf[32];
            std::snprintf(buf, sizeof buf, watts < 1.0 ? "%.1f W" : "%.0f W", watts);
            row.value = buf;
        } else {
            row.value = std::to_string(static_cast<int>(std::lround(row.score)));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string energyFootnote(const Frame& f) {
    if (f.energy.wattsSource == WattsSource::CpuPackage && f.energy.watts.isMeasured()) {
        return "Estimated from each process's share of CPU time × CPU package power.";
    }
    return "Relative demand from current CPU use; Linux does not publish per-process energy.";
}

std::string restrictedPowerHelp(const EnergySnapshot& e) {
    if (e.restrictedPath.empty()) return {};
    return "CPU power is readable by root only (" + e.restrictedPath +
           "). To grant read access without running PerfMonPlus as root, add your user to a group and install a udev "
           "rule — see the Linux notes in the README.";
}

}  // namespace pmp::vm
