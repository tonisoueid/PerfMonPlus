// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/installed_apps.hpp>
#include <perfmonplus/linux/kernel_modules.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pmp::vm {

struct AppRow {
    std::string key;  // source|id: identifies the app across refreshes
    std::string name, version, publisher, installed, size, lastUsed, location, source;
    linux::AppSource sourceKind = linux::AppSource::Dpkg;
    bool sizeKnown = false;
    bool installedExact = true;  // false: the date is estimated from a file timestamp
    double sortSize = 0;
};

struct AppsOptions {
    std::string filter{};
    std::optional<linux::AppSource> source{};
    std::string sortBy = "size";  // name | version | publisher | installed | size | location | source
    bool descending = true;
};

struct AppsSummary {
    std::string total;                                         // "12.4 GB in 2,544 apps"
    std::vector<std::pair<linux::AppSource, double>> bySource; // known bytes per source, largest first
    int unknownSize = 0;                                       // apps whose size no package record gives
    std::string note;                                          // "3 apps without a known size are not counted", or ""
};

std::vector<AppRow> buildApps(const std::vector<linux::InstalledApp>& apps, const AppsOptions& options);
AppsSummary summariseApps(const std::vector<linux::InstalledApp>& apps);

struct ModuleRow {
    std::string name, size, usedBy, state;
    double sortSize = 0;
};
std::vector<ModuleRow> buildModules(const std::vector<linux::KernelModule>& modules, const std::string& filter,
                                    const std::string& sortBy = "size", bool descending = true);

}  // namespace pmp::vm
