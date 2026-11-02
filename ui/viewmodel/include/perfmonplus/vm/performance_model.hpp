// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <string>
#include <vector>

namespace pmp::vm {

enum class Subsystem { Cpu, Memory, Disks, Network, Gpu, Npu, Energy, Thermals };

struct StatItem {
    std::string caption, value;
};

struct MasterEntry {
    Subsystem which;
    int device = 0;  // GPU / NPU index
    std::string title, line1, line2;
};

// The Performance page's left-hand cards (§7.2). Anything not measured reads "—" /
// "Unavailable" / "Restricted" via format(); a first frame never shows zeros.
// CPU, Memory, Disks, Network, then GPUs, NPUs, Energy and Thermals - only hardware that reports.
std::vector<MasterEntry> masterEntries(const Frame& f);

std::vector<StatItem> cpuStats(const Frame& f);      // Utilization, Processes, Threads, Up time, Logical processors
std::vector<StatItem> memoryStats(const Frame& f);   // In use ... Page outs (MEM-03 subset)
std::vector<StatItem> diskStats(const Frame& f);     // Read speed ... Type
std::vector<StatItem> networkStats(const Frame& f);
std::vector<StatItem> gpuStats(const GpuSnapshot& g);   // Utilization, memory, Frequency, Temperature, Power, Driver, PCI slot, engines
std::vector<StatItem> npuStats(const NpuSnapshot& n);   // Active time, Device  // Receive, Send, totals, "All interfaces", "Combined"

struct GridCell {
    int x, y, w, h;
};
// Wraps `count` cells into rows. At least one column; cells shrink to fit when the
// window is narrower than `minCellWidth`; cells never overlap and never leave [0, availableWidth].
std::vector<GridCell> gridLayout(int count, int availableWidth, int minCellWidth, int cellHeight, int gap);

}  // namespace pmp::vm
