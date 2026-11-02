// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/process_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <time.h>

#include <cstdio>

namespace pmp::vm {
namespace {

constexpr ColumnInfo kColumns[kColumnCount] = {
    {ProcessColumn::Name, "name", "Name", "", false},
    {ProcessColumn::Pid, "pid", "PID", "", true},
    {ProcessColumn::Status, "status", "Status", "", false},
    {ProcessColumn::User, "user", "User name", "", false},
    {ProcessColumn::Cpu, "cpu", "CPU usage", "Share of the whole machine's CPU", true},
    {ProcessColumn::Memory, "memory", "Memory usage", "Resident memory (the process's footprint in RAM)", true},
    {ProcessColumn::Gpu, "gpu", "GPU", "GPU engine use, from the kernel's DRM counters", true},
    {ProcessColumn::Npu, "npu", "NPU", "Per-process NPU use is not available on Linux", true},
    {ProcessColumn::DiskRead, "disk_read", "Disk read/s", "", true},
    {ProcessColumn::DiskWrite, "disk_write", "Disk write/s", "", true},
    {ProcessColumn::NetRx, "net_rx", "Network rx/s", "Per-process network use is not available on Linux", true},
    {ProcessColumn::NetTx, "net_tx", "Network tx/s", "Per-process network use is not available on Linux", true},
    {ProcessColumn::Energy, "energy", "Energy demand",
     "Relative demand from current CPU use; Linux does not publish per-process energy.", true},
    {ProcessColumn::Threads, "threads", "Threads", "", true},
    {ProcessColumn::PrivateMemory, "private", "Private memory", "Anonymous resident memory that is not shared", true},
    {ProcessColumn::PageFaults, "faults", "Page faults", "Minor plus major page faults since the process started", true},
    {ProcessColumn::Priority, "priority", "Priority", "The nice value: lower runs first", true},
    {ProcessColumn::StartTime, "started", "Start time", "", false},
    {ProcessColumn::CpuTime, "cpu_time", "CPU time", "Total CPU time used since the process started", true},
    {ProcessColumn::CommandLine, "cmdline", "Command line", "Collected only while this column is shown", false},
    {ProcessColumn::Path, "path", "Path", "", false},
    {ProcessColumn::Kind, "kind", "Kind", "App, Background or System", false},
};

std::string orDash(const std::string& s) { return s.empty() ? std::string("—") : s; }

std::string localTime(std::chrono::system_clock::time_point t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    tm parts{};
    if (!localtime_r(&tt, &parts)) return "—";
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &parts);
    return buf;
}

}  // namespace

const ColumnInfo& columnInfo(ProcessColumn c) { return kColumns[static_cast<int>(c)]; }

std::optional<ProcessColumn> columnFromKey(std::string_view key) {
    for (const auto& c : kColumns) {
        if (key == c.key) return c.id;
    }
    return std::nullopt;
}

std::vector<ProcessColumn> defaultColumns() {
    return {ProcessColumn::Name, ProcessColumn::Pid, ProcessColumn::Status, ProcessColumn::User, ProcessColumn::Cpu,
            ProcessColumn::Memory};
}

std::string statusText(char state, bool tombstone) {
    if (tombstone) return "exited";
    switch (state) {
        case 'R': return "running";
        case 'S':
        case 'I': return "sleeping";
        case 'D': return "waiting for I/O";
        case 'T': return "suspended";
        case 't': return "stopped";
        case 'Z': return "zombie";
        case 'X':
        case 'x': return "dead";
        default: return "—";
    }
}

std::string kindText(ProcessKind k) {
    switch (k) {
        case ProcessKind::App: return "App";
        case ProcessKind::Background: return "Background";
        case ProcessKind::System: return "System";
    }
    return "—";
}

Cells processCells(const ProcessRecord& r, const CellContext& ctx) {
    Cells c;
    const auto set = [&](ProcessColumn col, std::string v) { c[static_cast<std::size_t>(col)] = std::move(v); };
    set(ProcessColumn::Name, r.name);
    set(ProcessColumn::Pid, std::to_string(r.key.pid));
    set(ProcessColumn::Status, statusText(r.state, r.exitedAt.has_value()));
    set(ProcessColumn::User, orDash(r.user));
    set(ProcessColumn::Cpu, format(r.cpu));
    set(ProcessColumn::Memory, format(r.memFootprint));
    set(ProcessColumn::Gpu, format(r.gpu));
    set(ProcessColumn::Npu, "Unavailable");
    set(ProcessColumn::DiskRead, format(r.diskReadBps));
    set(ProcessColumn::DiskWrite, format(r.diskWriteBps));
    set(ProcessColumn::NetRx, "Unavailable");
    set(ProcessColumn::NetTx, "Unavailable");
    if (!r.cpu.isMeasured()) set(ProcessColumn::Energy, "—");
    else if (ctx.maxCpuPct <= 0) set(ProcessColumn::Energy, "0");
    else set(ProcessColumn::Energy, format(Metric::measured(*r.cpu.value / ctx.maxCpuPct * 100.0, Unit::Count)));
    set(ProcessColumn::Threads, format(r.threads));
    set(ProcessColumn::PrivateMemory, format(r.memPrivate));
    set(ProcessColumn::PageFaults, format(r.pageFaults));
    set(ProcessColumn::Priority, format(r.priority));
    set(ProcessColumn::StartTime, r.started ? localTime(*r.started) : std::string("—"));
    set(ProcessColumn::CpuTime, format(r.cpuTime));
    set(ProcessColumn::CommandLine, orDash(r.cmdline));
    set(ProcessColumn::Path, r.exeRestricted ? std::string("Restricted") : orDash(r.exe));
    set(ProcessColumn::Kind, kindText(r.kind));
    return c;
}

}  // namespace pmp::vm
