// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/process_actions.hpp>
#include <perfmonplus/linux/process_inspector.hpp>
#include <perfmonplus/linux/process_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

extern char** environ;

using namespace pmp;
using namespace pmp::linux;
using namespace std::chrono_literals;

namespace {

struct Child {
    pid_t pid = 0;
    ProcessKey key;
    Child() {
        char arg0[] = "sleep";
        char arg1[] = "60";
        char* argv[] = {arg0, arg1, nullptr};
        REQUIRE(posix_spawnp(&pid, "sleep", nullptr, nullptr, argv, environ) == 0);
        const auto stat = readText(ProcRoot::live().proc / std::to_string(pid) / "stat");
        REQUIRE(stat.ok());
        const auto parsed = parseProcStat(stat.text);
        REQUIRE(parsed);
        key = ProcessKey{pid, parsed->startTicks};
    }
    ~Child() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int st = 0;
            ::waitpid(pid, &st, 0);
        }
    }
    bool alive() const { return ::kill(pid, 0) == 0; }
    int reap() {
        int st = 0;
        ::waitpid(pid, &st, 0);
        const int s = st;
        pid = 0;
        return s;
    }
    ProcessTarget target() const { return ProcessTarget{key, "sleep", false}; }
    char state() const {
        const auto stat = readText(ProcRoot::live().proc / std::to_string(pid) / "stat");
        const auto parsed = parseProcStat(stat.text);
        return parsed ? parsed->state : '?';
    }
};

}  // namespace

TEST_CASE("term ends a real child", "[live][actions]") {
    Child c;
    const auto r = ProcessActions().signal(c.target(), Signal::Term);
    REQUIRE(r.ok);
    const int st = c.reap();
    REQUIRE(WIFSIGNALED(st));
    REQUIRE(WTERMSIG(st) == SIGTERM);
}

TEST_CASE("stop then cont changes the process state", "[live][actions]") {
    Child c;
    ProcessActions actions;
    REQUIRE(actions.signal(c.target(), Signal::Stop).ok);
    for (int i = 0; i < 100 && c.state() != 'T'; ++i) std::this_thread::sleep_for(10ms);
    REQUIRE(c.state() == 'T');
    REQUIRE(actions.signal(c.target(), Signal::Cont).ok);
    for (int i = 0; i < 100 && c.state() == 'T'; ++i) std::this_thread::sleep_for(10ms);
    REQUIRE(c.state() != 'T');
}

TEST_CASE("the live child survives a wrong-identity request", "[live][actions]") {
    Child c;
    auto stale = c.target();
    stale.key.startTicks += 1;  // what a recycled PID looks like to us
    const auto r = ProcessActions().signal(stale, Signal::Kill);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message.find("now belongs to a different process. No action taken.") != std::string::npos);
    REQUIRE(c.alive());
    REQUIRE(c.state() != 'Z');
}

TEST_CASE("a reaped child is reported as exited", "[live][actions]") {
    Child c;
    const auto t = c.target();
    ::kill(c.pid, SIGKILL);
    c.reap();
    const auto r = ProcessActions().signal(t, Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message.find("has already exited") != std::string::npos);
}

TEST_CASE("nice can be raised for our own child", "[live][actions]") {
    Child c;
    REQUIRE(ProcessActions().setPriority(c.target(), 10).ok);
    errno = 0;
    REQUIRE(getpriority(PRIO_PROCESS, static_cast<id_t>(c.pid)) == 10);
}

TEST_CASE("pid 1 cannot be ended by an unprivileged user", "[live][actions]") {
    if (geteuid() == 0) SKIP("root may signal pid 1");
    const auto stat = readText(ProcRoot::live().proc / "1" / "stat");
    const auto parsed = parseProcStat(stat.text);
    REQUIRE(parsed);
    const auto r = ProcessActions().signal(ProcessTarget{ProcessKey{1, parsed->startTicks}, "init", false}, Signal::Term);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message.find("do not have permission") != std::string::npos);
}

TEST_CASE("the inspector reads our own command line exactly", "[live][inspector]") {
    const auto stat = readText(ProcRoot::live().proc / "self" / "stat");
    const auto parsed = parseProcStat(stat.text);
    REQUIRE(parsed);
    const auto got = ProcessInspector().inspect(ProcessKey{static_cast<std::int32_t>(getpid()), parsed->startTicks}, false);
    REQUIRE(got);
    auto raw = readText(ProcRoot::live().proc / "self" / "cmdline").text;
    for (auto& c : raw) {
        if (c == '\0') c = ' ';
    }
    while (!raw.empty() && raw.back() == ' ') raw.pop_back();
    REQUIRE(got->cmdline == raw);
    REQUIRE_FALSE(got->architecture.empty());
    REQUIRE(got->openFiles.has_value());
}

TEST_CASE("another user's executable is Restricted, not empty", "[live][process]") {
    if (geteuid() == 0) SKIP("root can read every executable link");
    ProcessOptions o;
    o.selfPid = getpid();
    ProcessProvider provider(ProcRoot::live(), std::move(o));
    Frame f;
    provider.sample(f, SampleContext{1, MonoTime{}});
    bool sawRestricted = false;
    for (const auto& r : f.processes.processes) {
        if (r.uid == 0 && r.key.pid == 1) {
            sawRestricted = r.exeRestricted && r.exe.empty();
            REQUIRE(r.kind == ProcessKind::System);
        }
    }
    REQUIRE(sawRestricted);  // /proc/1/exe is unreadable to an ordinary user
}
