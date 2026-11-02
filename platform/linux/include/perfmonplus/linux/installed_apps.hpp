// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>
#include <perfmonplus/linux/command.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmp::linux {

enum class AppSource { Dpkg, Rpm, Flatpak, Snap, AppImage };

struct InstalledApp {
    std::string id;                      // package name, Flatpak application id, snap name or file name
    std::string name, version, publisher;  // sanitised; "" when unknown
    AppSource source = AppSource::Dpkg;
    Metric sizeBytes = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);  // from package metadata
    std::optional<std::chrono::system_clock::time_point> installed;
    std::string location;                // install directory or file; "" when unknown
    std::string iconName;                // for the desktop icon theme; "" when none
    bool userInstall = false;            // per-user Flatpak, or an AppImage in the home folder
    bool installedExact = true;          // false: the date is an estimate (an apt package without a dpkg log entry)
};

std::string sourceText(AppSource s);     // "apt", "rpm", "Flatpak", "Snap", "AppImage"

// Where each source keeps its records (injectable so tests can use a fake tree).
struct AppsRoot {
    std::filesystem::path dpkgStatus{"/var/lib/dpkg/status"};
    std::filesystem::path dpkgInfo{"/var/lib/dpkg/info"};
    std::vector<std::filesystem::path> dpkgLogs{"/var/log/dpkg.log.1", "/var/log/dpkg.log"};  // oldest first
    std::filesystem::path rpmDb{"/var/lib/rpm"};
    std::filesystem::path snapFiles{"/var/lib/snapd/snaps"};
    std::filesystem::path snapMounts{"/snap"};
    std::vector<std::filesystem::path> appImageDirs;
    static AppsRoot live();              // appImageDirs: ~/Applications, ~/.local/bin, ~/Downloads
};

// Pure parsers, exposed for tests.
std::vector<InstalledApp> parseDpkgStatus(std::string_view text);
std::vector<InstalledApp> parseFlatpakList(std::string_view text);   // application,name,version,origin,installation,size
std::vector<InstalledApp> parseRpmList(std::string_view text);       // name\tversion\tvendor\tsize\tinstalltime
std::optional<double> parseHumanSize(std::string_view text);         // "9.7 GB" -> 9.7e9 (decimal, as Flatpak prints it)

class InstalledAppsCollector {
public:
    InstalledAppsCollector(AppsRoot root, CommandRunner run) : root_(std::move(root)), run_(std::move(run)) {}
    // Every source is optional: a missing tool or file adds a note to `skipped` and nothing else. `cancel` is
    // checked between sources and between files.
    std::vector<InstalledApp> collect(const std::atomic<bool>& cancel, std::vector<std::string>* skipped = nullptr) const;
    // Where an apt package keeps its files, worked out from dpkg's file list when asked (reading 2,500 lists up front
    // would cost seconds). "" when the package has no directory of its own.
    std::string locate(const InstalledApp& app) const;
    // The exact command a person can run to remove the app, as plain text. Never executed by PerfMonPlus.
    static std::string uninstallHint(const InstalledApp& app);
    // The one removal PerfMonPlus will run itself: a per-user Flatpak (no administrator rights involved).
    static std::optional<std::vector<std::string>> userUninstallCommand(const InstalledApp& app);

private:
    AppsRoot root_;
    CommandRunner run_;
};

}  // namespace pmp::linux
