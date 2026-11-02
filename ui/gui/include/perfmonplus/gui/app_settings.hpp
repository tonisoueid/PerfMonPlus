// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/pages.hpp>

#include <QByteArray>
#include <QSettings>
#include <QStringList>

#include <chrono>
#include <string>

namespace pmp::gui {

struct AppSettings {
    std::string themeName = "dark";
    int saturation = 7;
    bool bloom = true;
    bool lowEffects = false;  // forces bloom off and caps the frame rate at 30 (NFR-01)
    int frameRate = 60;
    int historySeconds = 60;
    bool sidebarCollapsed = false;
    int lastPage = 0;
    int sampleIntervalMs = 1000;  // {0 (paused), 500, 1000, 2000, 5000}
    int topProcessCount = 12;     // 5..30
    bool decimalUnits = false;    // KB = 1000 instead of KiB = 1024
    bool fahrenheit = false;
    bool checkUpdates = true;
    std::string updateManifestUrl;  // empty: the update check is off
    bool launchAtLogin = false;
    QStringList disabledProviders;  // provider ids the user switched off
    // Processes page (§7.3)
    int processView = 0;                       // 0 grouped, 1 tree, 2 flat, 3 by user
    std::string processSort = "memory";        // a vm::ProcessColumn key
    bool processSortDescending = true;
    QStringList processColumns = {"name", "pid", "status", "user", "cpu", "memory"};  // column keys, "name" first
    int tombstoneSeconds = 10;                 // how long an exited process stays visible: 0 (off), 5, 10, 30, 60
    bool processHistoryEnabled = true;         // per-process history for hover details and culprit lookup
    bool showEnvironment = false;              // environment variables in the process details: off unless asked for
    bool detailsPaneOpen = true;
    int videoFps = 30;                 // screen video recording (§8.3): 15 or 30
    bool resolveHostNames = false;     // Connections: reverse DNS for remote addresses (off: nothing leaves the machine)
    // Flight Recorder (§8.2)
    bool recordingRing = false;        // record continuously into a rolling buffer, "save the last N minutes"
    int recordingRingMinutes = 10;     // 1..60
    int recordingMaxMinutes = 30;      // 5..120: a manual recording stops itself here
    int recordingTopProcesses = 20;    // 5..50 top processes per sample
    QStringList recentRecordings;      // at most 8, newest first, existing files only
    QByteArray geometry;

    void addRecent(const QString& path);  // moves it to the front, keeps 8

    // Everything the user can change in the app, but not the window's own state (geometry, last page, sidebar).
    void takeUserChoices(const AppSettings& o);
    // The interval the sampling thread starts with: "paused" (0) still needs a real interval for resuming.
    std::chrono::milliseconds startupInterval() const {
        return std::chrono::milliseconds(sampleIntervalMs > 0 ? sampleIntervalMs : 1000);
    }

    static AppSettings load(QSettings& store);
    void save(QSettings& store) const;
    friend bool operator==(const AppSettings&, const AppSettings&) = default;
};

}  // namespace pmp::gui
