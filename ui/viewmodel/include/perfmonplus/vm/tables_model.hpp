// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/services.hpp>
#include <perfmonplus/linux/sessions.hpp>
#include <perfmonplus/linux/startup.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace pmp::vm {

enum class StatusFilter { All, Running, Stopped, Failed };

struct ServiceRow {
    std::string unit, description, status, startType, pid, scope;
};

// status: Running (active/running) | Exited (active/exited) | Failed | Stopped (inactive) | the raw sub-state.
// `filter` is a case-insensitive substring of the unit name or description.
std::vector<ServiceRow> serviceRows(const std::vector<linux::ServiceInfo>& services, std::string_view filter,
                                    StatusFilter status);

struct StartupRow {
    std::string name, status, impact, source, command;
};
// status Enabled | Disabled; impact is "Not measured" (boot-time cost is not available on Linux);
// source "Autostart (you)" | "Autostart (system)" | "systemd user unit".
std::vector<StartupRow> startupRows(const std::vector<linux::StartupEntry>& entries);

struct UserRow {
    std::string user, sessions, status, cpu, memory, diskPerSec;
    std::vector<std::string> sessionIds;  // the user's login sessions (manager sessions excluded)
};
// One row per user with a login session. CPU, memory and disk are summed over that user's live processes in the
// frame; anything not measured stays "—".
std::vector<UserRow> userRows(const std::vector<linux::SessionInfo>& sessions, const Frame& f);

}  // namespace pmp::vm
