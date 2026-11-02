// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <functional>
#include <optional>
#include <string>

namespace pmp::linux {

// Slow or private facts about one process, read on demand for the selected row only.
struct Inspection {
    std::string cmdline, environment;
    std::string architecture;  // "x86_64" | "arm64" | "i386" | "arm" | "riscv64" | "" when unknown
    bool cmdlineRestricted = false, environmentRestricted = false, environmentTruncated = false;
    std::optional<int> openFiles, sockets;  // nullopt: not readable by this user
};

class ProcessInspector {
public:
    explicit ProcessInspector(ProcRoot root = ProcRoot::live()) : root_(std::move(root)) {}

    // nullopt when the process is gone or its PID now belongs to another process (INT-05). The environment is
    // read only when `withEnvironment` is set: it is opt-in because it often holds secrets.
    std::optional<Inspection> inspect(ProcessKey key, bool withEnvironment) const;

    // Test hook: runs after the reads and before the identity is checked a second time.
    std::function<void()> beforeFinalCheck;

private:
    ProcRoot root_;
};

}  // namespace pmp::linux
