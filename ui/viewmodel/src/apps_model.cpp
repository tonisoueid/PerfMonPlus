// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/apps_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>

namespace pmp::vm {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string dateText(const std::optional<std::chrono::system_clock::time_point>& t) {
    if (!t) return "—";
    const auto d = std::chrono::floor<std::chrono::days>(*t);
    const std::chrono::year_month_day ymd{d};
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                       static_cast<unsigned>(ymd.day()));
}

std::string orDash(const std::string& s) { return s.empty() ? "—" : s; }

std::string count(std::size_t n) {
    auto s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

}  // namespace

std::vector<AppRow> buildApps(const std::vector<linux::InstalledApp>& apps, const AppsOptions& opt) {
    std::vector<AppRow> rows;
    const auto needle = lower(opt.filter);
    for (const auto& a : apps) {
        if (opt.source && a.source != *opt.source) continue;
        AppRow r;
        r.key = linux::sourceText(a.source) + "|" + a.id;
        r.name = a.name.empty() ? a.id : a.name;
        r.version = orDash(a.version);
        r.publisher = orDash(a.publisher);
        r.installed = dateText(a.installed);
        r.installedExact = a.installedExact;
        r.sizeKnown = a.sizeBytes.isMeasured();
        r.size = format(a.sizeBytes);
        r.sortSize = r.sizeKnown ? *a.sizeBytes.value : 0;
        r.lastUsed = "Not tracked";  // no Linux package system records when an app was last used
        r.location = orDash(a.location);
        r.source = linux::sourceText(a.source);
        r.sourceKind = a.source;
        if (!needle.empty() && lower(r.name + " " + r.publisher + " " + r.source + " " + a.id).find(needle) == std::string::npos) continue;
        rows.push_back(std::move(r));
    }
    auto text = [&](const AppRow& r) -> const std::string& {
        if (opt.sortBy == "version") return r.version;
        if (opt.sortBy == "publisher") return r.publisher;
        if (opt.sortBy == "installed") return r.installed;
        if (opt.sortBy == "location") return r.location;
        if (opt.sortBy == "source") return r.source;
        return r.name;
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const AppRow& a, const AppRow& b) {
        if (opt.sortBy == "size") {
            if (a.sizeKnown != b.sizeKnown) return a.sizeKnown;  // unknown sizes sort last whichever way
            if (a.sortSize != b.sortSize) return opt.descending ? a.sortSize > b.sortSize : a.sortSize < b.sortSize;
            return lower(a.name) < lower(b.name);
        }
        const auto x = lower(text(a)), y = lower(text(b));
        if (x == y) return a.key < b.key;
        return opt.descending ? x > y : x < y;
    });
    return rows;
}

AppsSummary summariseApps(const std::vector<linux::InstalledApp>& apps) {
    AppsSummary s;
    std::map<linux::AppSource, double> by;
    double total = 0;
    for (const auto& a : apps) {
        if (a.sizeBytes.isMeasured()) {
            by[a.source] += *a.sizeBytes.value;
            total += *a.sizeBytes.value;
        } else {
            ++s.unknownSize;
        }
    }
    for (const auto& [src, bytes] : by) s.bySource.emplace_back(src, bytes);
    std::sort(s.bySource.begin(), s.bySource.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    s.total = format(Metric::measured(total, Unit::Bytes)) + " in " + count(apps.size()) + (apps.size() == 1 ? " app" : " apps");
    if (s.unknownSize > 0) {
        s.note = std::to_string(s.unknownSize) + (s.unknownSize == 1 ? " app without a known size is" : " apps without a known size are") +
                 " not counted";
    }
    return s;
}

std::vector<ModuleRow> buildModules(const std::vector<linux::KernelModule>& modules, const std::string& filter,
                                    const std::string& sortBy, bool descending) {
    std::vector<ModuleRow> rows;
    const auto needle = lower(filter);
    for (const auto& m : modules) {
        ModuleRow r;
        r.name = m.name;
        r.sortSize = static_cast<double>(m.sizeBytes);
        r.size = format(Metric::measured(r.sortSize, Unit::Bytes));
        for (const auto& u : m.usedBy) r.usedBy += (r.usedBy.empty() ? "" : ", ") + u;
        if (r.usedBy.empty()) r.usedBy = "—";
        r.state = m.state;
        if (!needle.empty() && lower(r.name + " " + r.usedBy).find(needle) == std::string::npos) continue;
        rows.push_back(std::move(r));
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const ModuleRow& a, const ModuleRow& b) {
        if (sortBy == "size") {
            if (a.sortSize != b.sortSize) return descending ? a.sortSize > b.sortSize : a.sortSize < b.sortSize;
            return a.name < b.name;
        }
        const auto x = lower(sortBy == "state" ? a.state : sortBy == "usedby" ? a.usedBy : a.name);
        const auto y = lower(sortBy == "state" ? b.state : sortBy == "usedby" ? b.usedBy : b.name);
        if (x == y) return a.name < b.name;
        return descending ? x > y : x < y;
    });
    return rows;
}

}  // namespace pmp::vm
