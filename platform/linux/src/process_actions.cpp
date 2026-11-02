// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/process_actions.hpp>
#include <perfmonplus/linux/process_provider.hpp>

#include <perfmonplus/linux/identity.hpp>

#include <sched.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace pmp::linux {
namespace {

std::string who(const ProcessTarget& t) { return t.name + " (PID " + std::to_string(t.key.pid) + ")"; }

std::string gone(const ProcessTarget& t, bool reused) {
    if (reused) {
        return who(t) + " has exited; PID " + std::to_string(t.key.pid) +
               " now belongs to a different process. No action taken.";
    }
    return who(t) + " has already exited. No action taken.";
}

std::string denial(const ProcessTarget& t, const char* verb) {
    return std::string("You do not have permission to ") + verb + " " + who(t) +
           ". It belongs to another user or to the system.";
}

int signalNumber(Signal s) {
    switch (s) {
        case Signal::Term: return SIGTERM;
        case Signal::Kill: return SIGKILL;
        case Signal::Stop: return SIGSTOP;
        case Signal::Cont: return SIGCONT;
    }
    return SIGTERM;
}

const char* verbOf(Signal s) {
    switch (s) {
        case Signal::Term: return "end";
        case Signal::Kill: return "force quit";
        case Signal::Stop: return "suspend";
        case Signal::Cont: return "resume";
    }
    return "signal";
}

std::string doneText(const ProcessTarget& t, Signal s) {
    switch (s) {
        case Signal::Term: return "Asked " + who(t) + " to end.";
        case Signal::Kill: return "Force quit " + who(t) + ".";
        case Signal::Stop: return "Suspended " + who(t) + ".";
        case Signal::Cont: return "Resumed " + who(t) + ".";
    }
    return {};
}

}  // namespace

Syscalls Syscalls::real() {
    Syscalls s;
    s.pidfdOpen = [](pid_t pid) {
        const long fd = ::syscall(SYS_pidfd_open, pid, 0);
        return fd < 0 ? -errno : static_cast<int>(fd);
    };
    s.pidfdSendSignal = [](int fd, int sig) {
        return ::syscall(SYS_pidfd_send_signal, fd, sig, nullptr, 0) < 0 ? -errno : 0;
    };
    s.close = [](int fd) { ::close(fd); };
    s.setPriority = [](pid_t pid, int nice) {
        return ::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), nice) < 0 ? -errno : 0;
    };
    s.setAffinity = [](pid_t pid, const std::vector<int>& cpus) {
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int cpu : cpus) {
            if (cpu >= 0 && cpu < CPU_SETSIZE) CPU_SET(cpu, &set);
        }
        return ::sched_setaffinity(pid, sizeof set, &set) < 0 ? -errno : 0;
    };
    s.getAffinity = [](pid_t pid) -> std::optional<std::vector<int>> {
        cpu_set_t set;
        CPU_ZERO(&set);
        if (::sched_getaffinity(pid, sizeof set, &set) < 0) return std::nullopt;
        std::vector<int> cpus;
        for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &set)) cpus.push_back(i);
        }
        return cpus;
    };
    return s;
}

ProcessActions::ProcessActions(ProcRoot root, Syscalls sys) : root_(std::move(root)), sys_(std::move(sys)) {}

ActionResult ProcessActions::signal(const ProcessTarget& t, Signal s) const {
    if (t.kernelThread) return {false, who(t) + " is a kernel thread and cannot be ended or suspended.", ActionOutcome::KernelThread};
    if (const auto id = verifyIdentity(root_, t.key); id != IdentityCheck::Alive) {
        return {false, gone(t, id == IdentityCheck::Reused), ActionOutcome::Gone};
    }
    if (const auto st = readText(root_.proc / std::to_string(t.key.pid) / "stat"); st.ok()) {
        if (const auto parsed = parseProcStat(st.text); parsed && parsed->state == 'Z') {
            return {false, who(t) + " has already ended and is waiting for its parent to collect it; ending it again does nothing.", ActionOutcome::Zombie};
        }
    }
    const int fd = sys_.pidfdOpen(t.key.pid);
    if (fd < 0) {
        if (fd == -ESRCH) return {false, gone(t, false), ActionOutcome::Gone};
        return {false, "Could not reach " + who(t) + ": " + std::strerror(-fd) + ".", ActionOutcome::Other};
    }
    // The pidfd now pins this exact process. Check once more that it is the one the user chose: the PID could
    // have been recycled between the first check and the open.
    if (const auto id = verifyIdentity(root_, t.key); id != IdentityCheck::Alive) {
        sys_.close(fd);
        return {false, gone(t, id == IdentityCheck::Reused), ActionOutcome::Gone};
    }
    const int rc = sys_.pidfdSendSignal(fd, signalNumber(s));
    sys_.close(fd);
    if (rc == 0) return {true, doneText(t, s), ActionOutcome::Done};
    if (rc == -ESRCH) return {false, gone(t, false), ActionOutcome::Gone};
    if (rc == -EPERM || rc == -EACCES) return {false, denial(t, verbOf(s)), ActionOutcome::Denied};
    return {false, std::string("Could not ") + verbOf(s) + " " + who(t) + ": " + std::strerror(-rc) + ".", ActionOutcome::Other};
}

ActionResult ProcessActions::signalTree(const std::vector<ProcessTarget>& targets, Signal s) const {
    int sent = 0, gone_ = 0, denied = 0, other = 0;
    std::string firstProblem;
    for (const auto& t : targets) {
        const auto r = signal(t, s);
        if (r.ok) {
            ++sent;
        } else if (r.outcome == ActionOutcome::Gone || r.outcome == ActionOutcome::Zombie) {
            ++gone_;  // a zombie is as good as gone: it only waits for its parent
        } else if (r.outcome == ActionOutcome::Denied) {
            ++denied;
        } else {
            ++other;
            if (firstProblem.empty()) firstProblem = r.message;
        }
    }
    std::string msg = std::to_string(sent) + " signalled, " + std::to_string(gone_) + " had already exited";
    if (denied > 0) msg += ", " + std::to_string(denied) + " could not be signalled (permission)";
    if (other > 0) msg += ". " + firstProblem;
    return {denied == 0 && other == 0, msg};
}

ActionResult ProcessActions::setPriority(const ProcessTarget& t, int nice) const {
    if (nice < -20 || nice > 19) return {false, "The nice value must be between -20 and 19."};
    if (const auto id = verifyIdentity(root_, t.key); id != IdentityCheck::Alive) {
        return {false, gone(t, id == IdentityCheck::Reused)};
    }
    const int rc = sys_.setPriority(t.key.pid, nice);
    // setpriority takes a PID number, not a handle: confirm afterwards that it was still this process.
    if (verifyIdentity(root_, t.key) != IdentityCheck::Alive) {
        return {false, who(t) + " exited while its priority was being changed; the change may have affected a different process."};
    }
    if (rc == 0) return {true, "Set the nice value of " + who(t) + " to " + std::to_string(nice) + "."};
    if (rc == -EACCES) return {false, "Lowering the nice value needs privileges PerfMonPlus does not have."};
    if (rc == -EPERM) return {false, denial(t, "change the priority of")};
    if (rc == -ESRCH) return {false, gone(t, false)};
    return {false, "Could not change the priority of " + who(t) + ": " + std::strerror(-rc) + "."};
}

ActionResult ProcessActions::setAffinity(const ProcessTarget& t, const std::vector<int>& cpus) const {
    if (cpus.empty()) return {false, "Choose at least one CPU."};
    if (const auto id = verifyIdentity(root_, t.key); id != IdentityCheck::Alive) {
        return {false, gone(t, id == IdentityCheck::Reused)};
    }
    const int rc = sys_.setAffinity(t.key.pid, cpus);
    if (verifyIdentity(root_, t.key) != IdentityCheck::Alive) {
        return {false, who(t) + " exited while its CPU affinity was being changed; the change may have affected a different process."};
    }
    if (rc == 0) return {true, "Updated the CPU affinity of " + who(t) + "."};
    if (rc == -EPERM || rc == -EACCES) return {false, denial(t, "change the CPU affinity of")};
    if (rc == -ESRCH) return {false, gone(t, false)};
    return {false, "Could not change the CPU affinity of " + who(t) + ": " + std::strerror(-rc) + "."};
}

std::optional<std::vector<int>> ProcessActions::affinityOf(ProcessKey key) const {
    if (verifyIdentity(root_, key) != IdentityCheck::Alive) return std::nullopt;
    return sys_.getAffinity(key.pid);
}

}  // namespace pmp::linux
