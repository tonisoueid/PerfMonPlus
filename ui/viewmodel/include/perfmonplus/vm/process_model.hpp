// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmp::vm {

// The Processes table's columns (PRC-03/04). Architecture lives in the details pane only (it needs a read of the
// executable), and "Footprint" is not a separate column: Memory usage already is the resident footprint.
enum class ProcessColumn : int {
    Name, Pid, Status, User, Cpu, Memory, Gpu, Npu, DiskRead, DiskWrite, NetRx, NetTx, Energy, Threads,
    PrivateMemory, PageFaults, Priority, StartTime, CpuTime, CommandLine, Path, Kind, Count_
};
inline constexpr int kColumnCount = static_cast<int>(ProcessColumn::Count_);

struct ColumnInfo {
    ProcessColumn id;
    const char* key;      // persisted in settings
    const char* title;    // header text
    const char* tooltip;  // header tooltip
    bool numeric;         // right-aligned, sorted by value
};
const ColumnInfo& columnInfo(ProcessColumn c);
std::optional<ProcessColumn> columnFromKey(std::string_view key);
std::vector<ProcessColumn> defaultColumns();  // PRC-03: Name, PID, Status, User name, CPU usage, Memory usage

std::string statusText(char state, bool tombstone);
std::string kindText(ProcessKind k);

struct CellContext {
    MonoTime now{};
    std::chrono::system_clock::time_point wallNow{};
    double totalMemoryBytes = 0;
    double maxCpuPct = 0;  // the busiest process: the scale of the relative Energy estimate
};

using Cells = std::array<std::string, kColumnCount>;
Cells processCells(const ProcessRecord& r, const CellContext& ctx);

}  // namespace pmp::vm
