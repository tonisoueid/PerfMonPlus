// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/process_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;
using pmp::linux::ProcessOptions;
using pmp::linux::ProcessProvider;

namespace {

constexpr const char* kStat =
    "cpu  100 0 100 800 0 0 0 0 0 0\n"
    "cpu0 50 0 50 400 0 0 0 0 0 0\n"
    "cpu1 50 0 50 400 0 0 0 0 0 0\n";

constexpr const char* kPid1Stat = "1 (init) S 0 1 1 0 -1 4194560 0 0 0 0 10 10 0 0 20 0 1 0 1 1000 100 0";

std::string pid42Stat(int utime, int stime, int start = 5000) {
    return "42 (my (weird) proc) S 1 42 42 0 -1 4194304 100 0 0 0 " + std::to_string(utime) + " " +
           std::to_string(stime) + " 0 0 20 0 3 0 " + std::to_string(start) + " 1000000 2560 0";
}

void populate(FakeRoot& fr) {
    fr.write("proc/stat", kStat);
    fr.write("proc/1/stat", kPid1Stat);
    fr.write("proc/1/status", "Uid:\t0\t0\t0\t0\n");
    fr.write("proc/42/stat", pid42Stat(200, 100));
    fr.write("proc/42/status", "Name:\tmy (weird) proc\nUid:\t1000\t1000\t1000\t1000\nRssAnon:\t    4096 kB\n");
    fr.write("proc/42/io", "read_bytes: 0\nwrite_bytes: 0\n");
}

ProcessOptions opts(bool includeSelf = false) {
    ProcessOptions o{.selfPid = 99, .userName = [](std::uint32_t uid) { return uid == 1000 ? "dave" : "root"; }};
    o.includeSelf = includeSelf;
    o.tombstoneRetention = 10s;  // the default is 60 s; most tests want the short one
    return o;
}

Frame sampleAt(ProcessProvider& p, int gen, MonoTime t) {
    Frame f;
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), t});
    return f;
}

const ProcessRecord* find(const Frame& f, std::int32_t pid, std::uint64_t start) {
    for (const auto& r : f.processes.processes) {
        if (r.key.pid == pid && r.key.startTicks == start) return &r;
    }
    return nullptr;
}

const MonoTime t0{};
const MonoTime t1 = t0 + 1s;

}  // namespace

TEST_CASE("stat parsing survives parentheses in comm", "[process]") {
    const auto s = pmp::linux::parseProcStat(pid42Stat(200, 100));
    REQUIRE(s);
    REQUIRE(s->pid == 42);
    REQUIRE(s->comm == "my (weird) proc");
    REQUIRE(s->state == 'S');
    REQUIRE(s->ppid == 1);
    REQUIRE(s->utime == 200);
    REQUIRE(s->stime == 100);
    REQUIRE(s->threads == 3);
    REQUIRE(s->startTicks == 5000);
    REQUIRE(s->rssPages == 2560);
    REQUIRE_FALSE(pmp::linux::parseProcStat("garbage"));
}

TEST_CASE("process records carry identity, parent, user and memory", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessProvider p(fr.root(), opts());
    auto f = sampleAt(p, 1, t0);
    const auto* r = find(f, 42, 5000);
    REQUIRE(r);
    REQUIRE(r->name == "my (weird) proc");
    REQUIRE(r->parent);
    REQUIRE(*r->parent == ProcessKey{1, 1});
    REQUIRE(r->user == "dave");
    REQUIRE(*r->memFootprint.value == 2560.0 * 4096);
    REQUIRE(*r->memPrivate.value == 4096.0 * 1024);
    REQUIRE(*r->threads.value == 3);
    REQUIRE(r->cpu.availability == Availability::Pending);
    REQUIRE(*f.processes.count.value == 2);
}

TEST_CASE("cpu is normalised to the whole machine", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);
    fr.write("proc/42/stat", pid42Stat(250, 150));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(*find(f, 42, 5000)->cpu.value == Approx(50.0));
}

TEST_CASE("disk rate comes from io deltas", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);
    fr.write("proc/42/io", "read_bytes: 2048\nwrite_bytes: 0\n");
    auto f = sampleAt(p, 2, t1);
    REQUIRE(*find(f, 42, 5000)->diskReadBps.value == Approx(2048));
}

TEST_CASE("vanished process is skipped", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.makeDir("proc/44");
    ProcessProvider p(fr.root(), opts());
    auto f = sampleAt(p, 1, t0);
    for (const auto& r : f.processes.processes) REQUIRE(r.key.pid != 44);
}

TEST_CASE("a process whose status vanished after its stat was read is skipped, not shown half-filled", "[process][minor]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/77/stat", "77 (gone) S 1 77 77 0 -1 4194304 0 0 0 0 1 1 0 0 20 0 1 0 6000 1000 10 0");
    // proc/77/status is missing: the process exited between the two reads.
    ProcessProvider p(fr.root(), opts());
    auto f = sampleAt(p, 1, t0);
    REQUIRE(find(f, 77, 6000) == nullptr);
    REQUIRE(find(f, 42, 5000) != nullptr);
}

TEST_CASE("unreadable io is Restricted", "[process]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    populate(fr);
    fr.makeUnreadable("proc/42/io");
    ProcessProvider p(fr.root(), opts());
    auto f = sampleAt(p, 1, t0);
    REQUIRE(find(f, 42, 5000)->diskReadBps.availability == Availability::Restricted);
}

TEST_CASE("exited process becomes a tombstone then expires", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);
    fr.remove("proc/42");
    auto f = sampleAt(p, 2, t1);
    const auto* r = find(f, 42, 5000);
    REQUIRE(r);
    REQUIRE(r->exitedAt == t1);
    REQUIRE(*f.processes.count.value == 1);
    auto later = sampleAt(p, 3, t1 + 11s);
    REQUIRE(find(later, 42, 5000) == nullptr);
}

TEST_CASE("self is excluded unless requested", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/99/stat", "99 (probe) R 1 99 99 0 -1 0 0 0 0 0 5 5 0 0 20 0 1 0 7000 1000 10 0");
    fr.write("proc/99/status", "Uid:\t1000\t1000\t1000\t1000\n");
    {
        ProcessProvider p(fr.root(), opts());
        REQUIRE(find(sampleAt(p, 1, t0), 99, 7000) == nullptr);
    }
    {
        ProcessProvider p(fr.root(), opts(true));
        REQUIRE(find(sampleAt(p, 1, t0), 99, 7000) != nullptr);
    }
}

TEST_CASE("pid reuse yields a new key and a tombstone for the old one", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);
    fr.write("proc/42/stat", pid42Stat(10, 10, 9000));
    auto f = sampleAt(p, 2, t1);
    REQUIRE(find(f, 42, 9000));
    REQUIRE_FALSE(find(f, 42, 9000)->exitedAt);
    REQUIRE(find(f, 42, 5000));
    REQUIRE(find(f, 42, 5000)->exitedAt);
}

TEST_CASE("status is cached and refreshed every few generations", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);
    fr.write("proc/42/status", "Uid:\t1000\t1000\t1000\t1000\nRssAnon:\t    8192 kB\n");

    auto cached = sampleAt(p, 2, t0 + 1s);
    REQUIRE(*find(cached, 42, 5000)->memPrivate.value == 4096.0 * 1024);  // still the first read
    REQUIRE(find(cached, 42, 5000)->user == "dave");

    Frame refreshed;
    for (int gen = 3; gen <= 6; ++gen) refreshed = sampleAt(p, gen, t0 + std::chrono::seconds(gen - 1));
    REQUIRE(*find(refreshed, 42, 5000)->memPrivate.value == 8192.0 * 1024);
}

TEST_CASE("status refreshes are staggered across generations", "[process]") {
    FakeRoot fr;
    populate(fr);
    for (int pid = 100; pid <= 104; ++pid) {
        fr.write("proc/" + std::to_string(pid) + "/stat",
                 std::to_string(pid) + " (p) S 1 " + std::to_string(pid) + " 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 " +
                     std::to_string(2000 + pid) + " 1000 10 0");
        fr.write("proc/" + std::to_string(pid) + "/status", "Uid:\t1000\nRssAnon:\t 100 kB\n");
    }
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);
    for (int pid = 100; pid <= 104; ++pid) {
        fr.write("proc/" + std::to_string(pid) + "/status", "Uid:\t1000\nRssAnon:\t 200 kB\n");
    }
    int refreshedSoFar = 0;
    for (int gen = 2; gen <= 6; ++gen) {
        auto f = sampleAt(p, gen, t0 + std::chrono::seconds(gen - 1));
        int now = 0;
        for (int pid = 100; pid <= 104; ++pid) {
            if (*find(f, pid, 2000 + pid)->memPrivate.value == 200.0 * 1024) ++now;
        }
        REQUIRE(now - refreshedSoFar <= 2);  // never a thundering herd
        refreshedSoFar = now;
    }
    REQUIRE(refreshedSoFar == 5);
}

namespace {

// A stat line with the fields the new facts come from: minflt(10) majflt(12) utime(14) stime(15) nice(19) start(22).
std::string statWith(int pid, int ppid, int minflt, int majflt, int utime, int stime, int nice, int start) {
    return std::to_string(pid) + " (p" + std::to_string(pid) + ") S " + std::to_string(ppid) + " " + std::to_string(pid) +
           " " + std::to_string(pid) + " 0 -1 4194304 " + std::to_string(minflt) + " 0 " + std::to_string(majflt) + " 0 " +
           std::to_string(utime) + " " + std::to_string(stime) + " 0 0 20 " + std::to_string(nice) + " 3 0 " +
           std::to_string(start) + " 1000000 2560 0";
}

constexpr const char* kFirefoxCgroup =
    "0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-gnome-firefox-4445.scope\n";

}  // namespace

TEST_CASE("stat fields: faults, nice and cpu time", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/42/stat", statWith(42, 1, 10, 2, 300, 200, 5, 5000));
    ProcessProvider p(fr.root(), opts());
    const auto* r = find(sampleAt(p, 1, t0), 42, 5000);
    REQUIRE(r);
    REQUIRE(*r->pageFaults.value == 12);
    REQUIRE(*r->priority.value == 5);
    REQUIRE(*r->cpuTime.value == Approx(5.0));
    REQUIRE(r->cpuTime.unit == Unit::Seconds);
}

TEST_CASE("started comes from btime and start ticks", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/stat", std::string(kStat) + "btime 1700000000\n");
    fr.write("proc/42/stat", statWith(42, 1, 0, 0, 1, 1, 0, 100));
    ProcessProvider p(fr.root(), opts());
    const auto* r = find(sampleAt(p, 1, t0), 42, 100);
    REQUIRE(r);
    REQUIRE(r->started.has_value());
    REQUIRE(*r->started == std::chrono::system_clock::time_point{} + 1700000001s);
}

TEST_CASE("classify", "[process]") {
    using pmp::linux::appScopeOf;
    using pmp::linux::classifyProcess;
    using pmp::linux::ProcessClassInput;
    std::string app = std::string(kFirefoxCgroup).substr(3);
    app.pop_back();  // the trailing newline
    REQUIRE(classifyProcess({5, 2, 0, ""}) == ProcessKind::System);        // kernel thread (parent is kthreadd)
    REQUIRE(classifyProcess({2, 0, 0, ""}) == ProcessKind::System);        // kthreadd itself
    REQUIRE(classifyProcess({800, 1, 0, ""}) == ProcessKind::System);      // root
    REQUIRE(classifyProcess({801, 1, 999, ""}) == ProcessKind::System);    // system account
    REQUIRE(classifyProcess({4445, 1, 1000, app}) == ProcessKind::App);
    REQUIRE(appScopeOf(app) == "app-gnome-firefox-4445.scope");
    const std::string term = "/user.slice/user-1000.slice/user@1000.service/app.slice/app-org.gnome.Terminal.slice/vte-spawn-1.scope";
    REQUIRE(classifyProcess({9, 1, 1000, term}) == ProcessKind::App);
    REQUIRE(appScopeOf(term) == "app-org.gnome.Terminal.slice");
    REQUIRE(classifyProcess({7, 1, 1000, "/user.slice/user-1000.slice/user@1000.service/session.slice/org.gnome.Shell@wayland.service"}) ==
            ProcessKind::Background);
    REQUIRE(classifyProcess({7, 1, 1000, ""}) == ProcessKind::Background);
    REQUIRE(appScopeOf("/system.slice/cron.service").empty());
}

TEST_CASE("the provider classifies from cgroup and uid", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/42/cgroup", kFirefoxCgroup);
    ProcessProvider p(fr.root(), opts());
    const auto f = sampleAt(p, 1, t0);
    const auto* app = find(f, 42, 5000);
    REQUIRE(app);
    REQUIRE(app->kind == ProcessKind::App);
    REQUIRE(app->appScope == "app-gnome-firefox-4445.scope");
    REQUIRE(app->uid == 1000);
    REQUIRE(find(f, 1, 1)->kind == ProcessKind::System);
}

TEST_CASE("exe is the link target, and empty when there is none", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.symlink("/usr/bin/target", "proc/42/exe");
    ProcessProvider p(fr.root(), opts());
    const auto ok = find(sampleAt(p, 1, t0), 42, 5000);
    REQUIRE(ok->exe == "/usr/bin/target");
    REQUIRE_FALSE(ok->exeRestricted);
    // No exe link at all (a kernel thread): unknown, but not restricted.
    FakeRoot fr2;
    populate(fr2);
    ProcessProvider p2(fr2.root(), opts());
    const auto none = find(sampleAt(p2, 1, t0), 42, 5000);
    REQUIRE(none->exe.empty());
    REQUIRE_FALSE(none->exeRestricted);
}

TEST_CASE("tombstones live for sixty seconds by default", "[process]") {
    FakeRoot fr;
    populate(fr);
    ProcessOptions o;  // defaults
    o.selfPid = 99;
    o.userName = [](std::uint32_t) { return "x"; };
    ProcessProvider p(fr.root(), o);
    sampleAt(p, 1, t0);
    fr.remove("proc/42");
    sampleAt(p, 2, t1);
    REQUIRE(find(sampleAt(p, 3, t1 + 59s), 42, 5000) != nullptr);
    REQUIRE(find(sampleAt(p, 4, t1 + 61s), 42, 5000) == nullptr);
}

TEST_CASE("command line is collected only on request", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/42/cmdline", std::string("firefox\0-new-window\0", 21));
    ProcessProvider p(fr.root(), opts());
    REQUIRE(find(sampleAt(p, 1, t0), 42, 5000)->cmdline.empty());
    p.setCollectCommandLine(true);
    REQUIRE(find(sampleAt(p, 2, t1), 42, 5000)->cmdline == "firefox -new-window");
    p.setCollectCommandLine(false);
    REQUIRE(find(sampleAt(p, 3, t1 + 1s), 42, 5000)->cmdline.empty());
}

TEST_CASE("new facts are not re-read every sample", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/42/cgroup", kFirefoxCgroup);
    ProcessProvider p(fr.root(), opts());
    REQUIRE(find(sampleAt(p, 1, t0), 42, 5000)->kind == ProcessKind::App);
    fr.write("proc/42/cgroup", "0::/user.slice/user-1000.slice/user@1000.service/session.slice/x.service\n");
    REQUIRE(find(sampleAt(p, 2, t1), 42, 5000)->kind == ProcessKind::App);  // cached on the status cadence
    ProcessKind later = ProcessKind::App;
    for (int g = 3; g <= 8; ++g) later = find(sampleAt(p, g, t1 + std::chrono::seconds(g)), 42, 5000)->kind;
    REQUIRE(later == ProcessKind::Background);  // picked up on the next refresh
}

TEST_CASE("switching command lines on does not collapse the refresh stagger", "[process]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/43/stat", statWith(43, 1, 0, 0, 1, 1, 0, 5100));
    fr.write("proc/43/status", "Uid:\t1000\t1000\t1000\t1000\nRssAnon:\t 4096 kB\n");
    fr.write("proc/43/io", "read_bytes: 0\nwrite_bytes: 0\n");
    const char* appA = "0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-x.scope\n";
    const char* bg = "0::/user.slice/user-1000.slice/user@1000.service/session.slice/y.service\n";
    fr.write("proc/42/cgroup", appA);
    fr.write("proc/43/cgroup", appA);
    ProcessProvider p(fr.root(), opts());
    sampleAt(p, 1, t0);                 // pid 42 refreshes next at generation 4, pid 43 at generation 3
    fr.write("proc/42/cgroup", bg);
    fr.write("proc/43/cgroup", bg);
    p.setCollectCommandLine(true);      // must read only the command lines now, not everything again
    const auto g2 = sampleAt(p, 2, t0 + 1s);
    REQUIRE(find(g2, 42, 5000)->kind == ProcessKind::App);
    REQUIRE(find(g2, 43, 5100)->kind == ProcessKind::App);
    const auto g3 = sampleAt(p, 3, t0 + 2s);
    REQUIRE(find(g3, 43, 5100)->kind == ProcessKind::Background);  // its own turn
    REQUIRE(find(g3, 42, 5000)->kind == ProcessKind::App);         // not yet: the stagger survived
}

TEST_CASE("process names are sanitised: control characters and bidi controls never reach the table", "[process][minor]") {
    FakeRoot fr;
    populate(fr);
    fr.write("proc/42/stat", "42 (evil\tna\nme\xE2\x80\xAE) S 1 42 42 0 -1 4194304 100 0 0 0 200 100 0 0 20 0 3 0 5000 1000000 2560 0");
    ProcessProvider p(fr.root(), opts());
    const auto* r = find(sampleAt(p, 1, t0), 42, 5000);
    REQUIRE(r != nullptr);
    REQUIRE(r->name.find('\t') == std::string::npos);
    REQUIRE(r->name.find('\n') == std::string::npos);
    REQUIRE(r->name.find("\xE2\x80\xAE") == std::string::npos);  // U+202E RIGHT-TO-LEFT OVERRIDE could reverse the text next to it
}

TEST_CASE("an unreadable status file leaves the uid unknown, not root", "[process][minor]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    populate(fr);
    fr.makeUnreadable("proc/42/status");
    ProcessProvider p(fr.root(), opts());
    const auto* r = find(sampleAt(p, 1, t0), 42, 5000);
    REQUIRE(r != nullptr);
    REQUIRE(r->uid == pmp::linux::kUnknownUid);
}

TEST_CASE("kernel threads are recognised by the kernel's own flag, not by their parent's pid", "[process][minor]") {
    FakeRoot fr;
    populate(fr);
    // flags 0x00200000 = PF_KTHREAD; a user process whose parent happens to be pid 2 (inside a PID namespace) is not one
    fr.write("proc/42/stat", "42 (kworker) S 1 42 42 0 -1 2097152 100 0 0 0 200 100 0 0 20 0 3 0 5000 1000000 2560 0");
    fr.write("proc/43/stat", "43 (inns) S 2 43 43 0 -1 4194304 100 0 0 0 1 1 0 0 20 0 1 0 5001 1000000 2560 0");
    fr.write("proc/43/status", "Uid:\t1000\t1000\t1000\t1000\n");
    ProcessProvider p(fr.root(), opts());
    const auto f = sampleAt(p, 1, t0);
    REQUIRE(find(f, 42, 5000)->kernelThread);
    REQUIRE_FALSE(find(f, 43, 5001)->kernelThread);
    REQUIRE_FALSE(find(f, 1, 1)->kernelThread);
}
