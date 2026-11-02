// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/gui/processes_page.hpp>
#include <perfmonplus/linux/process_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>

extern char** environ;

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

// The whole path of the P2 exit gate on the real system: a real child process, the real provider, the real page and
// the real signal. (The recycled-PID refusal itself is the live test "the live child survives a wrong-identity request".)
TEST_CASE("end task on a real process: tombstone, then gone", "[gui][e2e]") {
    char a0[] = "sleep", a1[] = "600";
    char* argv[] = {a0, a1, nullptr};
    pid_t child = 0;
    REQUIRE(posix_spawnp(&child, "sleep", nullptr, nullptr, argv, environ) == 0);

    linux::ProcessOptions opts;
    opts.selfPid = getpid();
    linux::ProcessProvider provider(linux::ProcRoot::live(), opts);
    ProcessesPage page;
    page.resize(1000, 700);
    page.show();
    page.setView(vm::ProcessView::Flat);
    HistoryStore history;

    const auto sample = [&](int generation, std::chrono::seconds at) {
        Frame f;
        f.generation = static_cast<std::uint64_t>(generation);
        f.mono = MonoTime{} + at;
        f.wall = std::chrono::system_clock::now();
        f.memory.total = Metric::measured(8.0 * 1024 * 1024 * 1024, Unit::Bytes);
        provider.sample(f, SampleContext{f.generation, f.mono});
        page.update(f, history, f.mono);
        return f;
    };

    const auto f1 = sample(1, 100s);
    ProcessKey key{};
    for (const auto& r : f1.processes.processes) {
        if (r.key.pid == child) key = r.key;
    }
    REQUIRE(key.pid == child);
    REQUIRE(page.selectProcess(key));
    REQUIRE(page.selectedKey() == key);

    page.endSelectedTask(false);  // our own ordinary process: no confirmation, a real SIGTERM through a pidfd
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGTERM);

    sample(2, 101s);  // the provider sees it gone
    const int row = page.model()->rowOfId("p:" + std::to_string(key.pid) + ":" + std::to_string(key.startTicks));
    REQUIRE(row >= 0);
    REQUIRE(page.model()->data(page.model()->index(row, 0), ProcessTableModel::TombstoneRole).toBool());
    REQUIRE(page.selectedKey() == key);  // still selected as a tombstone

    sample(3, 130s);  // longer than the 10 s retention
    REQUIRE(page.model()->rowOfId("p:" + std::to_string(key.pid) + ":" + std::to_string(key.startTicks)) < 0);
    REQUIRE_FALSE(page.selectedKey().has_value());
}
