// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <QString>

namespace pmp::gui {

enum class Page {
    Summary,
    Performance,
    Processes,
    SystemInfo,
    StartupApps,
    Users,
    Services,
    PowerFreq,
    Connections,
    InstalledApps,
    DiskSpace,
    Benchmarks
};

inline constexpr int kPageCount = 12;
// Pages before this index are monitoring pages; the rest sit under the TOOLS divider (SHL-03).
inline constexpr int kFirstToolPage = static_cast<int>(Page::PowerFreq);

QString pageLabel(Page p);

}  // namespace pmp::gui
