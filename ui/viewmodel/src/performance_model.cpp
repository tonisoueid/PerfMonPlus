// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/performance_model.hpp>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/vm/hardware_model.hpp>

#include <algorithm>
#include <cctype>

namespace pmp::vm {
namespace {

// Counts print as plain integers (no thousands separator), as on the reference.
std::string plain(const Metric& m) {
    if (m.isMeasured() && m.value) return std::to_string(static_cast<long long>(*m.value));
    return format(m);
}

std::string percentOf(const Metric& part, const Metric& whole) {
    if (!part.isMeasured() || !whole.isMeasured() || *whole.value <= 0) return "—";
    return format(Metric::measured(*part.value / *whole.value * 100.0, Unit::Percent));
}

std::string diskTypeName(DiskType t) {
    switch (t) {
        case DiskType::SSD: return "SSD";
        case DiskType::HDD: return "HDD";
        case DiskType::Mixed: return "Mixed";
        case DiskType::Unknown: return "Unavailable";
    }
    return "Unavailable";
}

}  // namespace

std::vector<MasterEntry> masterEntries(const Frame& f) {
    std::vector<MasterEntry> out;

    out.push_back({Subsystem::Cpu, 0, "CPU", format(f.cpu.total),
                   f.cpu.logical.empty() ? "—" : std::to_string(f.cpu.logical.size()) + " logical processors"});

    const bool memKnown = f.memory.inUse.isMeasured() && f.memory.total.isMeasured();
    out.push_back({Subsystem::Memory, 0, "Memory",
                   memKnown ? format(f.memory.inUse) + " / " + format(f.memory.total) : "Unavailable",
                   percentOf(f.memory.inUse, f.memory.total)});

    std::string diskLine = "Unavailable";
    if (f.disks.devices.size() == 1) {
        const auto& d = f.disks.devices.front();
        diskLine = d.model.empty() ? d.name : d.model;
    } else if (f.disks.devices.size() > 1) {
        diskLine = std::to_string(f.disks.devices.size()) + " physical disks combined";
    }
    out.push_back({Subsystem::Disks, 0, "Disks", diskLine, format(f.disks.combined.activePct)});

    const auto physical = std::count_if(f.net.interfaces.begin(), f.net.interfaces.end(),
                                        [](const NetInterface& n) { return !n.isVirtual; });
    out.push_back({Subsystem::Network, 0, "Network",
                   physical > 0 ? std::to_string(physical) + " network interfaces combined" : "Unavailable",
                   "S: " + format(f.net.combined.txBps) + "  R: " + format(f.net.combined.rxBps)});

    for (const auto& g : f.gpus) out.push_back({Subsystem::Gpu, g.index, "GPU " + std::to_string(g.index), g.name, format(g.util)});
    for (const auto& n : f.npus) {
        out.push_back({Subsystem::Npu, n.index, "NPU " + std::to_string(n.index), n.name,
                       n.activePct.isMeasured() ? format(n.activePct) + " active time" : format(n.activePct)});
    }
    if (f.energy.watts.availability != Availability::Unsupported || f.energy.battery.present) {
        const auto t = energyTile(f);
        out.push_back({Subsystem::Energy, 0, "Energy", t.subtitle, t.value});
    }
    if (f.thermals.hotspot.availability != Availability::Unsupported) {
        const auto t = thermalsTile(f);
        out.push_back({Subsystem::Thermals, 0, "Thermals", t.subtitle, t.value});
    }
    return out;
}

std::vector<StatItem> cpuStats(const Frame& f) {
    return {{"Utilization", format(f.cpu.total)},
            {"Processes", plain(f.processes.count)},
            {"Threads", plain(f.processes.threadCount)},
            {"Up time", format(f.cpu.uptime)},
            {"Logical processors", f.cpu.logical.empty() ? "—" : std::to_string(f.cpu.logical.size())}};
}

std::vector<StatItem> memoryStats(const Frame& f) {
    const auto& m = f.memory;
    return {{"In use", format(m.inUse)},
            {"Available", format(m.available)},
            {"Committed", format(m.committed)},
            {"Cached", format(m.cached)},
            {"Swap used", format(m.swapUsed)},
            {"Swap available", format(m.swapAvailable)},
            {"Compressed", format(m.compressed)},
            {"Pressure load", format(m.pressureSomeAvg10)},
            {"Page ins", plain(m.pageIns)},
            {"Page outs", plain(m.pageOuts)}};
}

std::vector<StatItem> diskStats(const Frame& f) {
    const auto& d = f.disks.combined;
    return {{"Read speed", format(d.readBps)},
            {"Write speed", format(d.writeBps)},
            {"Total read", format(d.totalRead)},
            {"Total written", format(d.totalWritten)},
            {"Active time", format(d.activePct)},
            {"Avg. response time", format(d.avgResponseMs)},
            {"Capacity", format(d.capacity)},
            {"Type", diskTypeName(d.type)}};
}

std::vector<StatItem> networkStats(const Frame& f) {
    const auto& n = f.net.combined;
    return {{"Receive", format(n.rxBps)},
            {"Send", format(n.txBps)},
            {"Total received", format(n.totalRx)},
            {"Total sent", format(n.totalTx)},
            {"Interface name", "All interfaces"},
            {"Connection type", "Combined"}};
}

std::vector<StatItem> gpuStats(const GpuSnapshot& g) {
    std::vector<StatItem> s;
    s.push_back({"Utilization", format(g.util)});
    if (g.memTotal.isMeasured()) s.push_back({"Dedicated memory", format(g.memUsed) + " / " + format(g.memTotal)});
    else s.push_back({"GPU memory (resident)", format(g.memUsed)});
    s.push_back({"Frequency", format(g.freqHz)});
    s.push_back({"Temperature", format(g.temperature)});
    s.push_back({"Power", format(g.power)});
    s.push_back({"Driver", g.driver.empty() ? "Unavailable" : g.driver});
    s.push_back({"PCI slot", g.pciSlot.empty() ? "Unavailable" : g.pciSlot});
    for (const auto& e : g.engines) {
        std::string name = e.name;
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        s.push_back({name + " engine", format(e.util)});
    }
    return s;
}

std::vector<StatItem> npuStats(const NpuSnapshot& n) {
    return {{"Active time", format(n.activePct)}, {"Device", n.name.empty() ? "Unavailable" : n.name}};
}

std::vector<GridCell> gridLayout(int count, int availableWidth, int minCellWidth, int cellHeight, int gap) {
    std::vector<GridCell> cells;
    if (count <= 0) return cells;
    const int width = std::max(1, availableWidth);
    const int columns = std::max(1, (width + gap) / (std::max(1, minCellWidth) + gap));
    const int cellWidth = std::max(1, (width - (columns - 1) * gap) / columns);
    for (int i = 0; i < count; ++i) {
        const int col = i % columns, row = i / columns;
        cells.push_back({col * (cellWidth + gap), row * (cellHeight + gap), cellWidth, cellHeight});
    }
    return cells;
}

}  // namespace pmp::vm
