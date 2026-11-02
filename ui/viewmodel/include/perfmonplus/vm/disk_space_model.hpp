// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/disk_scan.hpp>

#include <string>
#include <vector>

namespace pmp::vm {

struct LargestRow {
    std::string key;      // the exact path: identifies the file, and is what Reveal/Trash act on
    std::string name, folder, size, modified, type;  // plain text, sanitised
    double sortSize = 0;
    std::int64_t sortTime = 0;
};

std::vector<LargestRow> buildLargest(const linux::ScanResult& r, const std::string& filter, const std::string& sortBy = "size",
                                     bool descending = true);

struct TypeRow {
    std::string type, files, size;
    double fraction = 0;  // of the bytes in regular files
};
std::vector<TypeRow> buildTypes(const linux::ScanResult& r, std::size_t top = 12);  // the largest types, then "Other"

// "Scanned 1,204,332 files in 18 folders · 14 inaccessible · 2 mount points skipped", plus " · results are partial".
std::string scanSummary(const linux::ScanResult& r);
std::string explainSkipped(const linux::ScanResult& r);  // what inaccessible/skipped/vanished mean, for a tooltip

}  // namespace pmp::vm
