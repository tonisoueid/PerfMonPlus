// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <sys/types.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pmp::linux {

enum class Signal { Term, Kill, Stop, Cont };

struct ProcessTarget {
    ProcessKey key;
    std::string name;
    bool kernelThread = false;
};

// What happened, for callers that need to act on it (never parse the message for this).
enum class ActionOutcome { Done, Gone, Denied, KernelThread, Zombie, Other };

struct ActionResult {
    bool ok = false;
    std::string message;  // user-facing text, shown as is
    ActionOutcome outcome = ActionOutcome::Other;
};

// The system calls behind the actions, injectable so refusal paths are testable without real processes.
// Each returns 0 (or a descriptor) on success and -errno on failure.
struct Syscalls {
    std::function<int(pid_t)> pidfdOpen;
    std::function<int(int fd, int sig)> pidfdSendSignal;
    std::function<void(int fd)> close;
    std::function<int(pid_t, int nice)> setPriority;
    std::function<int(pid_t, const std::vector<int>& cpus)> setAffinity;
    std::function<std::optional<std::vector<int>>(pid_t)> getAffinity;
    static Syscalls real();
};

// Every action first re-verifies (pid, start time) against /proc (INT-05): a recycled PID is refused and nothing
// is sent. Signals go through a pidfd, which cannot be redirected to another process; priority and affinity take
// a PID number, so they verify before and after and say so if the second check fails.
class ProcessActions {
public:
    explicit ProcessActions(ProcRoot root = ProcRoot::live(), Syscalls sys = Syscalls::real());

    ActionResult signal(const ProcessTarget&, Signal) const;
    // Targets are signalled in order (callers pass leaves first). A target that is already gone is counted, not an error.
    ActionResult signalTree(const std::vector<ProcessTarget>& leavesFirst, Signal) const;
    ActionResult setPriority(const ProcessTarget&, int nice) const;  // -20..19
    ActionResult setAffinity(const ProcessTarget&, const std::vector<int>& cpus) const;
    std::optional<std::vector<int>> affinityOf(ProcessKey) const;

private:
    ProcRoot root_;
    Syscalls sys_;
};

}  // namespace pmp::linux
