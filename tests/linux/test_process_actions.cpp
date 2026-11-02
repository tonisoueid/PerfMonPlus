// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/process_actions.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <string>

using namespace pmp;
using namespace pmp::linux;

namespace {

std::string statLine(int pid, int start) {
    return std::to_string(pid) + " (x) S 1 " + std::to_string(pid) + " 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 " +
           std::to_string(start) + " 1000 10 0";
}

struct Recorder {
    std::vector<std::string> calls;
    int openResult = 100;
    int signalResult = 0;
    int priorityResult = 0;
    int affinityResult = 0;
    std::function<void()> onOpen, onPriority, onAffinity;
    std::optional<std::vector<int>> affinity = std::vector<int>{0, 1, 2, 3};

    Syscalls sys() {
        Syscalls s;
        s.pidfdOpen = [this](pid_t pid) {
            calls.push_back("open " + std::to_string(pid));
            if (onOpen) onOpen();
            return openResult;
        };
        s.pidfdSendSignal = [this](int fd, int sig) {
            calls.push_back("signal " + std::to_string(fd) + " " + std::to_string(sig));
            return signalResult;
        };
        s.close = [this](int fd) { calls.push_back("close " + std::to_string(fd)); };
        s.setPriority = [this](pid_t pid, int nice) {
            calls.push_back("nice " + std::to_string(pid) + " " + std::to_string(nice));
            if (onPriority) onPriority();
            return priorityResult;
        };
        s.setAffinity = [this](pid_t pid, const std::vector<int>& cpus) {
            std::string c = "affinity " + std::to_string(pid);
            for (int cpu : cpus) c += " " + std::to_string(cpu);
            calls.push_back(c);
            if (onAffinity) onAffinity();
            return affinityResult;
        };
        s.getAffinity = [this](pid_t) { return affinity; };
        return s;
    }
    bool sent() const {
        for (const auto& c : calls)
            if (c.rfind("signal", 0) == 0) return true;
        return false;
    }
};

ProcessTarget target(int pid = 42, int start = 5000, std::string name = "firefox") {
    return ProcessTarget{ProcessKey{pid, static_cast<std::uint64_t>(start)}, std::move(name), false};
}

}  // namespace

TEST_CASE("a recycled pid is refused and nothing is signalled", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 9000));  // PID 42 now belongs to a different process
    Recorder rec;
    const auto r = ProcessActions(fr.root(), rec.sys()).signal(target(), Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message == "firefox (PID 42) has exited; PID 42 now belongs to a different process. No action taken.");
    REQUIRE_FALSE(rec.sent());
}

TEST_CASE("identity is re-checked after the pidfd is opened", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    rec.onOpen = [&] { fr.write("proc/42/stat", statLine(42, 9000)); };  // recycled while we were opening it
    const auto r = ProcessActions(fr.root(), rec.sys()).signal(target(), Signal::Kill);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message.find("now belongs to a different process") != std::string::npos);
    REQUIRE_FALSE(rec.sent());
    REQUIRE(rec.calls.back() == "close 100");  // the descriptor is never leaked
}

TEST_CASE("an exited process is reported, not signalled", "[actions]") {
    FakeRoot fr;  // no /proc/42
    Recorder rec;
    const auto r = ProcessActions(fr.root(), rec.sys()).signal(target(), Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message == "firefox (PID 42) has already exited. No action taken.");
    REQUIRE(rec.calls.empty());
}

TEST_CASE("kernel threads are refused", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    auto t = target(42, 5000, "kworker/0:1");
    t.kernelThread = true;
    const auto r = ProcessActions(fr.root(), rec.sys()).signal(t, Signal::Kill);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message == "kworker/0:1 (PID 42) is a kernel thread and cannot be ended or suspended.");
    REQUIRE(rec.calls.empty());
}

TEST_CASE("signals map to the right numbers and report what happened", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    ProcessActions actions(fr.root(), rec.sys());
    REQUIRE(actions.signal(target(), Signal::Term).ok);
    REQUIRE(actions.signal(target(), Signal::Kill).ok);
    REQUIRE(actions.signal(target(), Signal::Stop).ok);
    REQUIRE(actions.signal(target(), Signal::Cont).ok);
    std::vector<std::string> signals;
    for (const auto& c : rec.calls)
        if (c.rfind("signal", 0) == 0) signals.push_back(c);
    REQUIRE(signals == std::vector<std::string>{"signal 100 15", "signal 100 9", "signal 100 19", "signal 100 18"});
    REQUIRE(actions.signal(target(), Signal::Term).message == "Asked firefox (PID 42) to end.");
}

TEST_CASE("permission denied is explained", "[actions]") {
    FakeRoot fr;
    fr.write("proc/1/stat", statLine(1, 1));
    Recorder rec;
    rec.signalResult = -EPERM;
    const auto r = ProcessActions(fr.root(), rec.sys()).signal(target(1, 1, "systemd"), Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message ==
            "You do not have permission to end systemd (PID 1). It belongs to another user or to the system.");
    REQUIRE(rec.calls.back() == "close 100");
}

TEST_CASE("tree continues past a child that already exited", "[actions]") {
    FakeRoot fr;
    fr.write("proc/10/stat", statLine(10, 100));
    fr.write("proc/30/stat", statLine(30, 300));  // 20 has already gone
    Recorder rec;
    const auto r = ProcessActions(fr.root(), rec.sys())
                       .signalTree({target(10, 100, "a"), target(20, 200, "b"), target(30, 300, "c")}, Signal::Term);
    REQUIRE(r.ok);
    REQUIRE(r.message == "2 signalled, 1 had already exited");
}

TEST_CASE("a tree reports the ones it was not allowed to signal", "[actions]") {
    FakeRoot fr;
    fr.write("proc/10/stat", statLine(10, 100));
    Recorder rec;
    rec.signalResult = -EPERM;
    const auto r = ProcessActions(fr.root(), rec.sys()).signalTree({target(10, 100, "a")}, Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message.find("1 could not be signalled") != std::string::npos);
}

TEST_CASE("priority is verified before and after", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    ProcessActions actions(fr.root(), rec.sys());
    REQUIRE(actions.setPriority(target(), 10).ok);
    REQUIRE(rec.calls.back() == "nice 42 10");
    rec.onPriority = [&] { fr.write("proc/42/stat", statLine(42, 9000)); };  // recycled mid-change
    const auto r = actions.setPriority(target(), 5);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message.find("may have affected a different process") != std::string::npos);
}

TEST_CASE("priority range is validated", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    ProcessActions actions(fr.root(), rec.sys());
    REQUIRE_FALSE(actions.setPriority(target(), 25).ok);
    REQUIRE_FALSE(actions.setPriority(target(), -21).ok);
    REQUIRE(rec.calls.empty());
}

TEST_CASE("lowering the nice value without privilege is explained", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    rec.priorityResult = -EACCES;
    const auto r = ProcessActions(fr.root(), rec.sys()).setPriority(target(), -5);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message == "Lowering the nice value needs privileges PerfMonPlus does not have.");
    rec.priorityResult = -EPERM;
    REQUIRE(ProcessActions(fr.root(), rec.sys()).setPriority(target(), 5).message.find("do not have permission") != std::string::npos);
}

TEST_CASE("affinity applies the chosen cpus and refuses none", "[actions]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    ProcessActions actions(fr.root(), rec.sys());
    REQUIRE(actions.setAffinity(target(), {0, 2}).ok);
    REQUIRE(rec.calls.back() == "affinity 42 0 2");
    rec.calls.clear();
    REQUIRE_FALSE(actions.setAffinity(target(), {}).ok);
    REQUIRE(rec.calls.empty());
    REQUIRE(actions.affinityOf(ProcessKey{42, 5000}) == std::vector<int>{0, 1, 2, 3});
    REQUIRE_FALSE(actions.affinityOf(ProcessKey{42, 9999}).has_value());  // someone else's PID
}

TEST_CASE("ending a zombie explains that its parent must collect it", "[actions][minor]") {
    FakeRoot fr;
    fr.write("proc/42/stat", "42 (x) Z 1 42 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 5000 0 0 0");
    Recorder rec;
    const auto r = ProcessActions(fr.root(), rec.sys()).signal(target(), Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.outcome == ActionOutcome::Zombie);
    REQUIRE(r.message == "firefox (PID 42) has already ended and is waiting for its parent to collect it; ending it again does nothing.");
    REQUIRE(rec.calls.empty());  // no signal is sent to a zombie
}

TEST_CASE("every result carries a status, so callers never search the message", "[actions][minor]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(42, 5000));
    Recorder rec;
    ProcessActions actions(fr.root(), rec.sys());
    REQUIRE(actions.signal(target(), Signal::Term).outcome == ActionOutcome::Done);
    rec.signalResult = -EPERM;
    REQUIRE(actions.signal(target(), Signal::Term).outcome == ActionOutcome::Denied);
    rec.signalResult = -EIO;
    REQUIRE(actions.signal(target(), Signal::Term).outcome == ActionOutcome::Other);
    FakeRoot none;
    REQUIRE(ProcessActions(none.root(), rec.sys()).signal(target(), Signal::Term).outcome == ActionOutcome::Gone);
    auto k = target();
    k.kernelThread = true;
    REQUIRE(actions.signal(k, Signal::Term).outcome == ActionOutcome::KernelThread);
}

TEST_CASE("a tree counts by status even when the message wording changes", "[actions][minor]") {
    FakeRoot fr;
    fr.write("proc/10/stat", statLine(10, 100));
    fr.write("proc/20/stat", "20 (x) Z 1 20 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 200 0 0 0");
    Recorder rec;
    const auto r = ProcessActions(fr.root(), rec.sys()).signalTree({target(10, 100, "a"), target(20, 200, "z")}, Signal::Term);
    REQUIRE(r.ok);  // a zombie that is already ending is not a failure of the tree
    REQUIRE(r.message == "1 signalled, 1 had already exited");
}
