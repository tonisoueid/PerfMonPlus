// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace pmp::linux {

struct CommandResult {
    int exitCode = -1;
    std::string out, err;
    bool timedOut = false;
};

// Injectable so collectors can be tested without running real commands.
using CommandRunner = std::function<CommandResult(const std::vector<std::string>& argv)>;

// Runs `argv` directly (posix_spawnp, never a shell) with stdin closed. Output is captured; the process is
// killed at `timeout`. A program that cannot be started yields exitCode 127 and a message in `err`.
CommandResult runCommand(const std::vector<std::string>& argv,
                         std::chrono::milliseconds timeout = std::chrono::seconds(10));

// Queries get 10 s. A privileged action (start/stop/restart/enable/disable a unit, lock/terminate a session) may
// sit behind a polkit dialog while the user types a password, so it waits much longer.
inline constexpr std::chrono::seconds kQueryTimeout{10};
inline constexpr std::chrono::minutes kActionTimeout{5};
std::chrono::milliseconds commandTimeout(const std::vector<std::string>& argv);

// runCommand with commandTimeout(argv): what the collectors use unless a test injects a fake.
CommandResult runDefault(const std::vector<std::string>& argv);

// One line the user can read: "timed out waiting for authorisation", the program's own message, or a fallback.
std::string describeFailure(const CommandResult& r);

}  // namespace pmp::linux
