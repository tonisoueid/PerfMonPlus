// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/tables_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <algorithm>
#include <cctype>
#include <map>

namespace pmp::vm {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string statusOf(const linux::ServiceInfo& s) {
    if (s.active == "failed" || s.sub == "failed") return "Failed";
    if (s.active == "active" && s.sub == "running") return "Running";
    if (s.active == "active" && s.sub == "exited") return "Exited";
    if (s.active == "inactive") return "Stopped";
    return s.sub.empty() ? s.active : s.sub;
}

}  // namespace

std::vector<ServiceRow> serviceRows(const std::vector<linux::ServiceInfo>& services, std::string_view filter,
                                    StatusFilter status) {
    const std::string needle = lower(std::string(filter));
    std::vector<ServiceRow> rows;
    for (const auto& s : services) {
        const std::string st = statusOf(s);
        if (status == StatusFilter::Running && st != "Running") continue;
        if (status == StatusFilter::Stopped && st != "Stopped") continue;
        if (status == StatusFilter::Failed && st != "Failed") continue;
        if (!needle.empty() && lower(s.unit).find(needle) == std::string::npos &&
            lower(s.description).find(needle) == std::string::npos) {
            continue;
        }
        rows.push_back({s.unit, s.description, st, s.startType, s.mainPid > 0 ? std::to_string(s.mainPid) : "",
                        s.scope == linux::ServiceScope::User ? "User" : "System"});
    }
    return rows;
}

std::vector<StartupRow> startupRows(const std::vector<linux::StartupEntry>& entries) {
    std::vector<StartupRow> rows;
    for (const auto& e : entries) {
        const char* source = e.source == linux::StartupSource::XdgUser     ? "Autostart (you)"
                             : e.source == linux::StartupSource::XdgSystem ? "Autostart (system)"
                                                                            : "systemd user unit";
        rows.push_back({e.name, e.enabled ? "Enabled" : "Disabled", "Not measured", source, e.command});
    }
    return rows;
}

std::vector<UserRow> userRows(const std::vector<linux::SessionInfo>& sessions, const Frame& f) {
    std::vector<UserRow> rows;
    std::map<std::string, std::size_t> index;
    std::vector<bool> allIdle;
    for (const auto& s : sessions) {
        if (s.sessionClass == "manager") continue;
        auto [it, fresh] = index.try_emplace(s.user, rows.size());
        if (fresh) {
            rows.push_back({s.user, "", "", "—", "—", "—", {}});
            allIdle.push_back(true);
        }
        auto& row = rows[it->second];
        for (const std::string& label : {s.tty, s.seat}) {
            if (label.empty()) continue;
            if (!row.sessions.empty() && row.sessions.find(label) != std::string::npos) continue;
            row.sessions += (row.sessions.empty() ? "" : ", ") + label;
        }
        row.sessionIds.push_back(s.id);
        if (!s.idle) allIdle[it->second] = false;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) rows[i].status = allIdle[i] ? "Idle" : "Active";

    for (auto& row : rows) {
        double cpu = 0, mem = 0, disk = 0;
        bool haveCpu = false, haveMem = false, haveDisk = false;
        for (const auto& p : f.processes.processes) {
            if (p.exitedAt || p.user != row.user) continue;
            if (p.cpu.isMeasured()) { cpu += *p.cpu.value; haveCpu = true; }
            if (p.memFootprint.isMeasured()) { mem += *p.memFootprint.value; haveMem = true; }
            if (p.diskReadBps.isMeasured() && p.diskWriteBps.isMeasured()) {
                disk += *p.diskReadBps.value + *p.diskWriteBps.value;
                haveDisk = true;
            }
        }
        if (haveCpu) row.cpu = format(Metric::measured(cpu, Unit::Percent));
        if (haveMem) row.memory = format(Metric::measured(mem, Unit::Bytes));
        if (haveDisk) row.diskPerSec = format(Metric::measured(disk, Unit::BytesPerSec));
    }
    return rows;
}

}  // namespace pmp::vm
