// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace pmp::linux {

struct ProcStat {
    std::int32_t pid = 0;
    std::string comm;
    char state = '?';
    std::int32_t ppid = 0;
    std::uint64_t utime = 0, stime = 0;
    std::int64_t threads = 0;
    std::uint64_t startTicks = 0;
    std::uint64_t rssPages = 0;
    std::uint64_t minflt = 0, majflt = 0;
    std::int64_t nice = 0;
    std::uint64_t flags = 0;  // proc(5) field 9; 0x00200000 is PF_KTHREAD
};

inline constexpr std::uint32_t kUnknownUid = 0xFFFFFFFFu;  // the status file could not be read

struct ProcessClassInput {
    std::int32_t pid, ppid;
    std::uint32_t uid;
    std::string_view cgroup;  // the unified-hierarchy path, e.g. "/user.slice/.../app.slice/app-x.scope"
};
// System: kernel threads, root, system accounts (uid < 1000), system.slice. App: anything under app.slice
// (how GNOME and KDE launch applications). Everything else is Background.
ProcessKind classifyProcess(const ProcessClassInput& in);
// The first path component after "app.slice/" ("" when the process is not in app.slice).
std::string appScopeOf(std::string_view cgroup);

// Parses /proc/[pid]/stat. comm may contain spaces and parentheses, so fields
// after it are located from the LAST ')'. nullopt on malformed input.
std::optional<ProcStat> parseProcStat(std::string_view text);

struct ProcessOptions {
    std::int32_t selfPid = 0;
    bool includeSelf = false;
    // How long exited processes are kept. The view model filters by the user's setting, so this is the maximum.
    std::chrono::seconds tombstoneRetention{60};
    std::function<std::string(std::uint32_t uid)> userName;  // default: getpwuid_r, numeric fallback
};

// /proc/[pid]/status is the costliest file to read and only feeds slow-moving
// fields (user, private memory), so it is re-read every few generations.
struct ProcessStatusInfo {
    std::string user;
    Metric memPrivate;
    std::uint32_t uid = 0;
    std::string exe;
    bool exeRestricted = false;
    ProcessKind kind = ProcessKind::Background;
    std::string appScope;
    std::string cmdline;
    bool cmdlineRead = false;
    std::uint64_t nextRefresh = 0;  // generation at which to re-read; staggered by pid
};

class ProcessProvider final : public Provider {
public:
    ProcessProvider(ProcRoot root, ProcessOptions opts);

    std::string_view id() const override { return "processes"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void reset() override {
        prevTime_.reset();
        prev_.clear();
    }
    void fillUnavailable(Frame& out, Availability why) const override;

    // Command lines are private: collected only while a column or pane needs them. Thread-safe.
    void setCollectCommandLine(bool on) { collectCmdline_ = on; }

private:
    struct Prev {
        std::uint64_t ticks = 0;
        std::optional<std::uint64_t> readBytes, writeBytes;
    };

    ProcRoot root_;
    ProcessOptions opts_;
    std::atomic<bool> collectCmdline_{false};
    std::optional<std::uint64_t> btime_;  // boot time, seconds since the epoch
    std::map<ProcessKey, ProcessStatusInfo> statusCache_;
    std::optional<MonoTime> prevTime_;
    std::map<ProcessKey, Prev> prev_;
    std::map<ProcessKey, ProcessRecord> live_;
    std::map<ProcessKey, ProcessRecord> tombstones_;
};

}  // namespace pmp::linux
