// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/cpu_facts.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pmp::linux {

struct NetAdapter {
    std::string name, mac, ipv4, ipv6;
    Metric speedBps;  // BytesPerSec; Unavailable when the link speed cannot be read
    bool isVirtual = false;
};

struct SystemInfo {
    std::string hostname, osName, kernel, architecture, vendor, model, biosVersion;
    std::string serial;  // empty when it cannot be read (root only on most machines)
    Metric uptime, bootTime /*seconds since the epoch*/, memTotal, swapTotal;
    CpuFacts cpu;
    std::vector<std::string> gpus;
    std::vector<NetAdapter> adapters;
    std::optional<bool> secureBoot;  // nullopt when efivars are unreadable
};

// One-shot inventory (§7.4). Every missing piece is left empty / unavailable; nothing throws.
// `etc` is where os-release lives (a parameter so tests can use a fake tree).
SystemInfo collectSystemInfo(const ProcRoot& root, const std::filesystem::path& etc = "/etc");

}  // namespace pmp::linux
