// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/process_details.hpp>

#include <perfmonplus/core/format.hpp>

#include <format>

namespace pmp::vm {

ProcessDetails processDetails(const ProcessRecord& r, const ProcessRecord* parent, const linux::Inspection* inspected,
                              const CellContext& ctx) {
    const auto cells = processCells(r, ctx);
    const auto cell = [&](ProcessColumn c) { return cells[static_cast<std::size_t>(c)]; };

    ProcessDetails d;
    d.title = r.name + " (" + std::to_string(r.key.pid) + ")";
    d.path = r.exeRestricted ? "Restricted" : r.exe.empty() ? "Unavailable" : r.exe;

    const std::string arch = inspected && !inspected->architecture.empty() ? inspected->architecture : "—";
    d.sections.push_back({"IDENTITY",
                          {{"Parent", parent ? parent->name + " (" + std::to_string(parent->key.pid) + ")" : "—"},
                           {"User", cell(ProcessColumn::User)},
                           {"Status", cell(ProcessColumn::Status)},
                           {"Architecture", arch}}});

    std::string uptime = "—";
    if (r.started && !r.exitedAt && ctx.wallNow >= *r.started) {
        uptime = format(Metric::measured(std::chrono::duration<double>(ctx.wallNow - *r.started).count(), Unit::Seconds));
    }
    d.sections.push_back({"LIFETIME",
                          {{"Started", cell(ProcessColumn::StartTime)},
                           {"Uptime", uptime},
                           {"Threads", cell(ProcessColumn::Threads)},
                           {"Priority", cell(ProcessColumn::Priority)}}});
    d.sections.push_back({"PROCESSOR",
                          {{"CPU", cell(ProcessColumn::Cpu)},
                           {"CPU time", cell(ProcessColumn::CpuTime)},
                           {"GPU", cell(ProcessColumn::Gpu)},
                           {"NPU", cell(ProcessColumn::Npu)}}});
    d.sections.push_back({"MEMORY",
                          {{"Footprint", cell(ProcessColumn::Memory)},
                           {"Private", cell(ProcessColumn::PrivateMemory)},
                           {"Page faults", cell(ProcessColumn::PageFaults)}}});
    d.sections.push_back({"DISK", {{"Reading", cell(ProcessColumn::DiskRead)}, {"Writing", cell(ProcessColumn::DiskWrite)}}});

    if (r.exitedAt) d.exitText = "This process has exited. Linux does not report another process's exit status.";

    if (!inspected) d.openFiles = "—";
    else if (!inspected->openFiles) d.openFiles = "Restricted";
    else d.openFiles = std::to_string(*inspected->openFiles) + " open files, " + std::to_string(inspected->sockets.value_or(0)) + " sockets";
    return d;
}

std::vector<CulpritRow> culpritRows(const std::vector<ProcessHistory::Culprit>& culprits, ProcessHistory::Resource by) {
    std::vector<CulpritRow> rows;
    for (const auto& c : culprits) {
        CulpritRow r;
        r.name = c.name;
        r.exited = c.exited;
        r.key = c.key;
        if (by == ProcessHistory::Resource::Cpu) {
            r.primary = std::format("{:.1f} CPU-seconds", c.cpuSeconds);
            r.secondary = "avg " + format(Metric::measured(c.avgCpuPct, Unit::Percent));
        } else {
            r.primary = "peak " + format(Metric::measured(c.peakMemBytes, Unit::Bytes));
            r.secondary = "avg " + format(Metric::measured(c.avgMemBytes, Unit::Bytes));
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

std::string rowCopyText(const std::vector<std::string>& cells) {
    std::string out;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i) out += '\t';
        out += cells[i];
    }
    return out;
}

}  // namespace pmp::vm
