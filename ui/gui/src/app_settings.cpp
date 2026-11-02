// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/app_settings.hpp>

#include <QFileInfo>

#include <perfmonplus/vm/process_model.hpp>

#include <algorithm>

namespace pmp::gui {

QString pageLabel(Page p) {
    switch (p) {
        case Page::Summary: return "Summary";
        case Page::Performance: return "Performance";
        case Page::Processes: return "Processes";
        case Page::SystemInfo: return "System Info";
        case Page::StartupApps: return "Startup apps";
        case Page::Users: return "Users";
        case Page::Services: return "Services";
        case Page::PowerFreq: return "Power & Freq";
        case Page::Connections: return "Connections";
        case Page::InstalledApps: return "Installed Apps";
        case Page::DiskSpace: return "Disk Space";
        case Page::Benchmarks: return "Benchmarks";
    }
    return {};
}

void AppSettings::addRecent(const QString& path) {
    recentRecordings.removeAll(path);
    recentRecordings.prepend(path);
    while (recentRecordings.size() > 8) recentRecordings.removeLast();
}

void AppSettings::takeUserChoices(const AppSettings& o) {
    const auto geometryKept = geometry;
    const auto pageKept = lastPage;
    const auto sidebarKept = sidebarCollapsed;
    *this = o;
    geometry = geometryKept;
    lastPage = pageKept;
    sidebarCollapsed = sidebarKept;
}

AppSettings AppSettings::load(QSettings& s) {
    AppSettings d;
    AppSettings r;
    r.themeName = s.value("theme", QString::fromStdString(d.themeName)).toString().toStdString();
    r.saturation = std::clamp(s.value("saturation", d.saturation).toInt(), 0, 11);
    r.bloom = s.value("bloom", d.bloom).toBool();
    r.lowEffects = s.value("lowEffects", d.lowEffects).toBool();
    const int fps = s.value("frameRate", d.frameRate).toInt();
    r.frameRate = (fps == 15 || fps == 30 || fps == 60 || fps == 120) ? fps : d.frameRate;  // anything else would fall off the popover's choices
    const int hist = s.value("historySeconds", d.historySeconds).toInt();
    r.historySeconds = (hist >= 10 && hist <= 3600) ? hist : d.historySeconds;
    const int interval = s.value("sampleIntervalMs", d.sampleIntervalMs).toInt();
    r.sampleIntervalMs = (interval == 0 || interval == 500 || interval == 1000 || interval == 2000 || interval == 5000)
                             ? interval
                             : d.sampleIntervalMs;
    const int top = s.value("topProcessCount", d.topProcessCount).toInt();
    r.topProcessCount = (top >= 5 && top <= 30) ? top : d.topProcessCount;
    r.decimalUnits = s.value("decimalUnits", d.decimalUnits).toBool();
    r.fahrenheit = s.value("fahrenheit", d.fahrenheit).toBool();
    r.checkUpdates = s.value("checkUpdates", d.checkUpdates).toBool();
    r.updateManifestUrl = s.value("updateManifestUrl").toString().trimmed().toStdString();
    r.launchAtLogin = s.value("launchAtLogin", d.launchAtLogin).toBool();
    for (const QString& id : s.value("disabledProviders").toStringList()) {
        if (!id.isEmpty() && !r.disabledProviders.contains(id)) r.disabledProviders << id;
    }
    r.processView = std::clamp(s.value("processView", d.processView).toInt(), 0, 3);
    {
        const std::string sort = s.value("processSort", QString::fromStdString(d.processSort)).toString().toStdString();
        r.processSort = vm::columnFromKey(sort) ? sort : d.processSort;
    }
    r.processSortDescending = s.value("processSortDescending", d.processSortDescending).toBool();
    {
        QStringList cols;
        for (const QString& key : s.value("processColumns", d.processColumns).toStringList()) {
            if (vm::columnFromKey(key.toStdString()) && !cols.contains(key)) cols << key;
        }
        cols.removeAll("name");
        cols.prepend("name");  // the Name column is always there and always first
        r.processColumns = cols.size() > 1 ? cols : d.processColumns;
    }
    const int tomb = s.value("tombstoneSeconds", d.tombstoneSeconds).toInt();
    r.tombstoneSeconds = (tomb == 0 || tomb == 5 || tomb == 10 || tomb == 30 || tomb == 60) ? tomb : d.tombstoneSeconds;
    r.processHistoryEnabled = s.value("processHistoryEnabled", d.processHistoryEnabled).toBool();
    r.showEnvironment = s.value("showEnvironment", d.showEnvironment).toBool();
    const int vfps = s.value("videoFps", d.videoFps).toInt();
    r.videoFps = (vfps == 15 || vfps == 30) ? vfps : d.videoFps;
    r.resolveHostNames = s.value("resolveHostNames", d.resolveHostNames).toBool();
    r.recordingRing = s.value("recordingRing", d.recordingRing).toBool();
    r.recordingRingMinutes = std::clamp(s.value("recordingRingMinutes", d.recordingRingMinutes).toInt(), 1, 60);
    r.recordingMaxMinutes = std::clamp(s.value("recordingMaxMinutes", d.recordingMaxMinutes).toInt(), 5, 120);
    const int recTop = s.value("recordingTopProcesses", d.recordingTopProcesses).toInt();
    r.recordingTopProcesses = (recTop >= 5 && recTop <= 50) ? recTop : d.recordingTopProcesses;
    for (const QString& path : s.value("recentRecordings").toStringList()) {
        if (r.recentRecordings.size() < 8 && !r.recentRecordings.contains(path) && QFileInfo::exists(path)) r.recentRecordings << path;
    }
    r.detailsPaneOpen = s.value("detailsPaneOpen", d.detailsPaneOpen).toBool();
    r.sidebarCollapsed = s.value("sidebarCollapsed", d.sidebarCollapsed).toBool();
    r.lastPage = std::clamp(s.value("lastPage", d.lastPage).toInt(), 0, kPageCount - 1);
    r.geometry = s.value("geometry").toByteArray();
    return r;
}

void AppSettings::save(QSettings& s) const {
    s.setValue("theme", QString::fromStdString(themeName));
    s.setValue("saturation", saturation);
    s.setValue("bloom", bloom);
    s.setValue("lowEffects", lowEffects);
    s.setValue("frameRate", frameRate);
    s.setValue("historySeconds", historySeconds);
    s.setValue("sidebarCollapsed", sidebarCollapsed);
    s.setValue("lastPage", lastPage);
    s.setValue("processView", processView);
    s.setValue("processSort", QString::fromStdString(processSort));
    s.setValue("processSortDescending", processSortDescending);
    s.setValue("processColumns", processColumns);
    s.setValue("tombstoneSeconds", tombstoneSeconds);
    s.setValue("processHistoryEnabled", processHistoryEnabled);
    s.setValue("showEnvironment", showEnvironment);
    s.setValue("detailsPaneOpen", detailsPaneOpen);
    s.setValue("videoFps", videoFps);
    s.setValue("resolveHostNames", resolveHostNames);
    s.setValue("recordingRing", recordingRing);
    s.setValue("recordingRingMinutes", recordingRingMinutes);
    s.setValue("recordingMaxMinutes", recordingMaxMinutes);
    s.setValue("recordingTopProcesses", recordingTopProcesses);
    s.setValue("recentRecordings", recentRecordings);
    s.setValue("sampleIntervalMs", sampleIntervalMs);
    s.setValue("topProcessCount", topProcessCount);
    s.setValue("decimalUnits", decimalUnits);
    s.setValue("fahrenheit", fahrenheit);
    s.setValue("checkUpdates", checkUpdates);
    s.setValue("updateManifestUrl", QString::fromStdString(updateManifestUrl));
    s.setValue("launchAtLogin", launchAtLogin);
    s.setValue("disabledProviders", disabledProviders);
    s.setValue("geometry", geometry);
}

}  // namespace pmp::gui
