// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/disk_space_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>

namespace pmp::vm {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string count(std::uint64_t n) {
    auto s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

std::string date(std::int64_t t) {
    const auto d = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::time_point(std::chrono::seconds(t)));
    const std::chrono::year_month_day ymd{d};
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
}

}  // namespace

std::vector<LargestRow> buildLargest(const linux::ScanResult& r, const std::string& filter, const std::string& sortBy, bool descending) {
    std::vector<LargestRow> rows;
    const auto needle = lower(filter);
    for (const auto& f : r.largest) {
        LargestRow row;
        row.key = f.path;
        const auto slash = f.display.rfind('/');
        row.name = slash == std::string::npos ? f.display : f.display.substr(slash + 1);
        row.folder = slash == std::string::npos ? std::string() : (slash == 0 ? "/" : f.display.substr(0, slash));
        row.sortSize = static_cast<double>(f.bytes);
        row.size = format(Metric::measured(row.sortSize, Unit::Bytes));
        row.sortTime = f.mtime;
        row.modified = date(f.mtime);
        row.type = f.ext;
        if (!needle.empty() && lower(f.display + " " + f.ext).find(needle) == std::string::npos) continue;
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const LargestRow& a, const LargestRow& b) {
        if (sortBy == "size") return descending ? a.sortSize > b.sortSize : a.sortSize < b.sortSize;
        if (sortBy == "modified") return descending ? a.sortTime > b.sortTime : a.sortTime < b.sortTime;
        const auto& x = sortBy == "type" ? a.type : sortBy == "folder" ? a.folder : a.name;
        const auto& y = sortBy == "type" ? b.type : sortBy == "folder" ? b.folder : b.name;
        const auto lx = lower(x), ly = lower(y);
        if (lx == ly) return a.key < b.key;
        return descending ? lx > ly : lx < ly;
    });
    return rows;
}

std::vector<TypeRow> buildTypes(const linux::ScanResult& r, std::size_t top) {
    std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint64_t>>> all(r.byType.begin(), r.byType.end());
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
        if (a.second.second != b.second.second) return a.second.second > b.second.second;
        return a.first < b.first;
    });
    double total = 0;
    for (const auto& [t, v] : all) total += static_cast<double>(v.second);
    std::vector<TypeRow> rows;
    std::uint64_t otherFiles = 0, otherBytes = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (i < top) {
            TypeRow row;
            row.type = all[i].first;
            row.files = count(all[i].second.first);
            row.size = format(Metric::measured(static_cast<double>(all[i].second.second), Unit::Bytes));
            row.fraction = total > 0 ? static_cast<double>(all[i].second.second) / total : 0;
            rows.push_back(std::move(row));
        } else {
            otherFiles += all[i].second.first;
            otherBytes += all[i].second.second;
        }
    }
    if (otherFiles > 0) {
        TypeRow row;
        row.type = "Other";
        row.files = count(otherFiles);
        row.size = format(Metric::measured(static_cast<double>(otherBytes), Unit::Bytes));
        row.fraction = total > 0 ? static_cast<double>(otherBytes) / total : 0;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string scanSummary(const linux::ScanResult& r) {
    std::string s = "Scanned " + count(r.root.files) + (r.root.files == 1 ? " file" : " files") + " in " + count(r.dirs) +
                    (r.dirs == 1 ? " folder" : " folders");
    if (r.inaccessible > 0) s += " · " + count(r.inaccessible) + " inaccessible";
    if (r.skippedMounts > 0) s += " · " + count(r.skippedMounts) + (r.skippedMounts == 1 ? " mount point skipped" : " mount points skipped");
    if (r.vanished > 0) s += " · " + count(r.vanished) + " changed while scanning";
    if (r.cancelled || r.truncated) s += " · results are partial";
    return s;
}

std::string explainSkipped(const linux::ScanResult& r) {
    std::string s;
    if (r.inaccessible > 0) s += "Inaccessible: folders you do not have permission to read. Their size is not counted.\n";
    if (r.skippedMounts > 0) s += "Mount points skipped: other disks and system file systems mounted below the folder you chose.\n";
    if (r.vanished > 0) s += "Changed while scanning: files that were removed before they could be read.\n";
    if (r.truncated) s += "The folder tree is too large to keep in full; totals are still exact.\n";
    return s;
}

}  // namespace pmp::vm
