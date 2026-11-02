// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/linux/process_inspector.hpp>
#include <perfmonplus/vm/performance_model.hpp>
#include <perfmonplus/vm/process_model.hpp>

#include <string>
#include <utility>
#include <vector>

namespace pmp::vm {

// The bottom pane for one selected process (PRC-11): "{Name} ({PID})", the executable path and five columns.
struct ProcessDetails {
    std::string title;
    std::string path;       // the executable, "Restricted" or "Unavailable"
    std::vector<std::pair<std::string, std::vector<StatItem>>> sections;  // IDENTITY LIFETIME PROCESSOR MEMORY DISK
    std::string exitText;   // tombstones only
    std::string openFiles;  // "38 open files, 5 sockets", "Restricted" or "—"
};
ProcessDetails processDetails(const ProcessRecord& r, const ProcessRecord* parent, const linux::Inspection* inspected,
                              const CellContext& ctx);

struct CulpritRow {
    std::string name, primary, secondary;
    bool exited = false;
    ProcessKey key;
};
std::vector<CulpritRow> culpritRows(const std::vector<ProcessHistory::Culprit>& culprits, ProcessHistory::Resource by);

std::string rowCopyText(const std::vector<std::string>& cells);  // tab separated, for "Copy row"

}  // namespace pmp::vm
