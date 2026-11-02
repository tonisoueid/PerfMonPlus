// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/vm/process_model.hpp>

#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pmp::vm {

enum class ProcessView { Grouped, Tree, Flat, ByUser };

struct ProcessTableOptions {
    ProcessView view = ProcessView::Grouped;
    std::string filter;                        // "" = none; a case-insensitive substring of name, user or PID
    ProcessColumn sortBy = ProcessColumn::Memory;
    bool descending = true;
    std::set<std::string> toggled;             // row ids whose expanded state differs from the default
    std::vector<ProcessRecord> pinned;         // tombstones the user pinned: kept after the provider drops them
    int tombstoneSeconds = 10;                 // 0 = off
    std::optional<ProcessKey> reveal;          // open whatever hides this process (group, tree path, header)
};

struct TableRow {
    enum class Type { Header, Group, Process } type = Type::Process;
    std::string id;                            // "h:apps" "h:bg" "h:sys" "u:<user>" "a:<scope>" "p:<pid>:<startTicks>"
    ProcessKey key;                            // Process rows; the leader for Group rows
    std::vector<ProcessKey> members;           // Group rows: every process in the app (selection follows them)
    int depth = 0;
    bool expandable = false, expanded = false, tombstone = false, pinned = false;
    std::string title;                         // Header rows: "Applications (20)"
    Cells cells;
    std::array<double, kColumnCount> heat{};   // 0..1 for Cpu/Memory/Gpu/DiskRead/DiskWrite, -1 elsewhere (VIS-11)
    std::string tooltip;                       // tombstones: "Exited 4 s ago · ran 0:00:12 · exit status unavailable"
};

// One flat, ordered list of rows, ready to show. Expanded state is `default XOR (id in toggled)`: headers and
// roots default to expanded, groups and deeper tree nodes to collapsed; an active filter expands every ancestor of
// a match. Unmeasured values sort last whichever way the column is sorted; ties break by (pid, start time), so the
// order is stable between samples (PRC-05).
std::vector<TableRow> buildProcessTable(const Frame& f, const ProcessTableOptions& o, const CellContext& ctx);

}  // namespace pmp::vm
