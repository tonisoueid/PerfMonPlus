// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <cerrno>
#include <perfmonplus/linux/process_provider.hpp>

#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <vector>

namespace pmp::linux {
namespace {

std::string defaultUserName(std::uint32_t uid) {
    passwd pw{};
    passwd* res = nullptr;
    char buf[1024];
    if (getpwuid_r(uid, &pw, buf, sizeof buf, &res) == 0 && res) return res->pw_name;
    return std::to_string(uid);
}

template <class T>
bool parseNum(std::string_view s, T& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

Metric pending(Unit u) { return Metric::notMeasured(Availability::Pending, u); }

constexpr std::uint64_t kStatusRefreshGenerations = 5;


struct Scanned {
    ProcStat stat;
    ProcessStatusInfo status;
    std::optional<std::uint64_t> readBytes, writeBytes;
    Availability ioWhy = Availability::Measured;  // Measured => io was readable
};

}  // namespace

std::optional<ProcStat> parseProcStat(std::string_view text) {
    const auto open = text.find('(');
    const auto close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) return std::nullopt;

    ProcStat s;
    std::string_view pidStr = text.substr(0, open);
    while (!pidStr.empty() && pidStr.back() == ' ') pidStr.remove_suffix(1);
    if (!parseNum(pidStr, s.pid)) return std::nullopt;
    s.comm = std::string(text.substr(open + 1, close - open - 1));

    std::vector<std::string_view> tok;
    std::string_view rest = text.substr(close + 1);
    while (!rest.empty()) {
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\n')) rest.remove_prefix(1);
        const auto sp = rest.find_first_of(" \n");
        if (!rest.empty()) tok.push_back(rest.substr(0, sp));
        rest = sp == std::string_view::npos ? std::string_view{} : rest.substr(sp);
    }
    // field n of proc(5) is tok[n - 3]; we need up to field 24 (rss).
    if (tok.size() < 22 || tok[0].size() != 1) return std::nullopt;
    s.state = tok[0][0];
    parseNum(tok[6], s.flags);
    if (!parseNum(tok[1], s.ppid) || !parseNum(tok[7], s.minflt) || !parseNum(tok[9], s.majflt) ||
        !parseNum(tok[16], s.nice) || !parseNum(tok[11], s.utime) || !parseNum(tok[12], s.stime) ||
        !parseNum(tok[17], s.threads) || !parseNum(tok[19], s.startTicks) || !parseNum(tok[21], s.rssPages)) {
        return std::nullopt;
    }
    return s;
}

std::string cgroupPathOf(std::string_view text) {
    // Unified hierarchy: the line "0::/path". Older hierarchies have no such line: unknown.
    while (!text.empty()) {
        const auto nl = text.find('\n');
        const auto line = text.substr(0, nl);
        if (line.starts_with("0::")) return std::string(line.substr(3));
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return {};
}

std::string appScopeOf(std::string_view cgroup) {
    constexpr std::string_view marker = "/app.slice/";
    const auto at = cgroup.find(marker);
    if (at == std::string_view::npos) return {};
    auto rest = cgroup.substr(at + marker.size());
    return std::string(rest.substr(0, rest.find('/')));
}

ProcessKind classifyProcess(const ProcessClassInput& in) {
    if (in.pid == 2 || in.ppid == 2) return ProcessKind::System;  // kthreadd and its children
    if (in.uid != kUnknownUid && (in.uid == 0 || in.uid < 1000)) return ProcessKind::System;
    if (in.cgroup.find("/system.slice/") != std::string_view::npos) return ProcessKind::System;
    if (in.cgroup.find("/app.slice/") != std::string_view::npos) return ProcessKind::App;
    return ProcessKind::Background;
}

ProcessProvider::ProcessProvider(ProcRoot root, ProcessOptions opts)
    : root_(std::move(root)), opts_(std::move(opts)) {
    if (!opts_.userName) opts_.userName = defaultUserName;
}

void ProcessProvider::sample(Frame& out, const SampleContext& ctx) {
    out.processes = {};

    int logicalCpus = 0;
    Availability cpuWhy = Availability::Measured;
    {
        const auto st = readText(root_.proc / "stat");
        if (!st.ok()) {
            cpuWhy = availabilityForErrno(st.error);
        } else {
            std::string_view t = st.text;
            while (!t.empty()) {
                const auto nl = t.find('\n');
                const auto line = t.substr(0, nl);
                if (line.size() > 3 && line.starts_with("cpu") && line[3] >= '0' && line[3] <= '9') ++logicalCpus;
                if (!btime_ && line.starts_with("btime ")) {
                    std::uint64_t b = 0;
                    if (parseNum(line.substr(6), b)) btime_ = b;
                }
                if (nl == std::string_view::npos) break;
                t.remove_prefix(nl + 1);
            }
            if (logicalCpus == 0) logicalCpus = 1;
        }
    }

    std::vector<Scanned> scanned;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root_.proc, ec), endIt; !ec && it != endIt; it.increment(ec)) {
        const auto& e = *it;
        std::int32_t pid = 0;
        if (!parseNum(e.path().filename().string(), pid)) continue;
        if (pid == opts_.selfPid && !opts_.includeSelf) continue;

        const auto stat = readText(e.path() / "stat");
        if (!stat.ok()) continue;  // exited mid-scan, or unreadable: skip silently
        auto parsed = parseProcStat(stat.text);
        if (!parsed) continue;

        Scanned sc;
        sc.stat = std::move(*parsed);
        const ProcessKey key{sc.stat.pid, sc.stat.startTicks};
        const auto cached = statusCache_.find(key);
        const bool wantCmdline = collectCmdline_.load();
        const bool due = cached == statusCache_.end() || ctx.generation >= cached->second.nextRefresh;
        // Command lines switched on after an entry was cached: read just that file and keep the entry's own turn, so
        // the staggered refresh (which spreads the cost over a cycle) is not collapsed into one expensive sample.
        const bool cmdlineOnly = !due && wantCmdline && !cached->second.cmdlineRead;
        const bool fresh = !due && !cmdlineOnly;
        if (cmdlineOnly) {
            sc.status = cached->second;
            sc.status.cmdlineRead = true;
            const auto cmd = readText(e.path() / "cmdline");
            sc.status.cmdline = cmd.ok() ? sanitizeText(cmd.text, 8192) : std::string();
        } else if (fresh) {
            sc.status = cached->second;
        } else {
            // A cohort first seen in the same generation must not all refresh together:
            // offset the first refresh by pid so the cost spreads over a cycle.
            const std::uint64_t offset =
                cached == statusCache_.end() ? static_cast<std::uint64_t>(sc.stat.pid) % kStatusRefreshGenerations : 0;
            sc.status.nextRefresh = ctx.generation + kStatusRefreshGenerations - offset;
            sc.status.memPrivate = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
            const auto status = readText(e.path() / "status");
            sc.status.uid = kUnknownUid;
            if (status.ok()) {
                const auto kv = parseColonValues(status.text);
                if (const auto uid = kv.find("Uid"); uid != kv.end()) {
                    sc.status.uid = static_cast<std::uint32_t>(uid->second);
                    sc.status.user = opts_.userName(sc.status.uid);
                }
                if (const auto anon = kv.find("RssAnon"); anon != kv.end()) {
                    sc.status.memPrivate = Metric::measured(static_cast<double>(anon->second) * 1024.0, Unit::Bytes);
                }
            } else if (status.error == ENOENT || status.error == ESRCH) {
                continue;  // exited between the stat read and this one: a half-empty record would flash in the list
            } else {
                sc.status.memPrivate = Metric::notMeasured(availabilityForErrno(status.error), Unit::Bytes);
            }
        }
        if (due) {
            std::error_code lec;
            const auto target = std::filesystem::read_symlink(e.path() / "exe", lec);
            if (!lec) sc.status.exe = sanitizeText(target.string(), 4096);
            else sc.status.exeRestricted = lec == std::errc::permission_denied || lec == std::errc::operation_not_permitted;
            const auto cg = readText(e.path() / "cgroup");
            const std::string cgroup = cg.ok() ? cgroupPathOf(cg.text) : std::string();
            sc.status.kind = classifyProcess({sc.stat.pid, sc.stat.ppid, sc.status.uid, cgroup});
            sc.status.appScope = appScopeOf(cgroup);
            sc.status.cmdlineRead = wantCmdline;
            if (wantCmdline) {
                const auto cmd = readText(e.path() / "cmdline");
                if (cmd.ok()) sc.status.cmdline = sanitizeText(cmd.text, 8192);
            } else {
                sc.status.cmdline.clear();
            }
        }
        const auto io = readText(e.path() / "io");
        if (io.ok()) {
            const auto kv = parseColonValues(io.text);
            if (auto r = kv.find("read_bytes"); r != kv.end()) sc.readBytes = r->second;
            if (auto w = kv.find("write_bytes"); w != kv.end()) sc.writeBytes = w->second;
        } else {
            sc.ioWhy = availabilityForErrno(io.error);
        }
        scanned.push_back(std::move(sc));
    }
    if (ec) {
        fillUnavailable(out, availabilityForErrno(ec.value()));
        return;
    }
    std::sort(scanned.begin(), scanned.end(), [](const Scanned& a, const Scanned& b) { return a.stat.pid < b.stat.pid; });

    std::map<std::int32_t, std::uint64_t> startOf;
    for (const auto& s : scanned) startOf[s.stat.pid] = s.stat.startTicks;
    const bool wantCmdline = collectCmdline_.load();

    const double dt = prevTime_ ? std::chrono::duration<double>(ctx.mono - *prevTime_).count() : 0.0;
    std::map<ProcessKey, ProcessRecord> live;
    std::map<ProcessKey, Prev> prev;
    double threadSum = 0;

    for (const auto& s : scanned) {
        ProcessRecord r;
        r.key = {s.stat.pid, s.stat.startTicks};
        if (const auto p = startOf.find(s.stat.ppid); p != startOf.end()) r.parent = ProcessKey{s.stat.ppid, p->second};
        r.kernelThread = (s.stat.flags & 0x00200000u) != 0;
        r.name = sanitizeText(s.stat.comm, 128);  // comm is whatever the program set: no tabs, newlines or bidi controls
        r.user = s.status.user;
        r.state = s.stat.state;
        r.uid = s.status.uid;  // kUnknownUid when the status file could not be read: never mistaken for root
        r.exe = s.status.exe;
        r.exeRestricted = s.status.exeRestricted;
        r.kind = r.kernelThread ? ProcessKind::System : s.status.kind;
        r.appScope = s.status.appScope;
        r.cmdline = wantCmdline ? s.status.cmdline : std::string();
        r.cpuTime = Metric::measured(static_cast<double>(s.stat.utime + s.stat.stime) / static_cast<double>(root_.clockTicksPerSec), Unit::Seconds);
        r.priority = Metric::measured(static_cast<double>(s.stat.nice), Unit::Count);
        r.pageFaults = Metric::measured(static_cast<double>(s.stat.minflt + s.stat.majflt), Unit::Count);
        if (btime_) {
            r.started = std::chrono::system_clock::time_point{} + std::chrono::seconds(*btime_) +
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(
                            std::chrono::duration<double>(static_cast<double>(s.stat.startTicks) / static_cast<double>(root_.clockTicksPerSec)));
        }
        r.memFootprint = Metric::measured(static_cast<double>(s.stat.rssPages) * static_cast<double>(root_.pageSize), Unit::Bytes);
        r.memPrivate = s.status.memPrivate;
        r.threads = Metric::measured(static_cast<double>(s.stat.threads), Unit::Count);
        threadSum += static_cast<double>(s.stat.threads);

        const std::uint64_t ticks = s.stat.utime + s.stat.stime;
        const auto old = prev_.find(r.key);
        const bool haveOld = old != prev_.end() && dt > 0;

        if (cpuWhy != Availability::Measured) {
            r.cpu = Metric::notMeasured(cpuWhy, Unit::Percent);
        } else if (haveOld && ticks >= old->second.ticks) {
            const double cpuSeconds = static_cast<double>(ticks - old->second.ticks) / static_cast<double>(root_.clockTicksPerSec);
            r.cpu = Metric::measured(cpuSeconds / dt / logicalCpus * 100.0, Unit::Percent);
        } else {
            r.cpu = pending(Unit::Percent);
        }

        const auto rate = [&](const std::optional<std::uint64_t>& cur, const std::optional<std::uint64_t> Prev::*field) {
            if (s.ioWhy != Availability::Measured) return Metric::notMeasured(s.ioWhy, Unit::BytesPerSec);
            if (!cur) return Metric::notMeasured(Availability::Unsupported, Unit::BytesPerSec);
            if (haveOld && (old->second.*field) && *cur >= *(old->second.*field)) {
                return Metric::measured(static_cast<double>(*cur - *(old->second.*field)) / dt, Unit::BytesPerSec);
            }
            return pending(Unit::BytesPerSec);
        };
        r.diskReadBps = rate(s.readBytes, &Prev::readBytes);
        r.diskWriteBps = rate(s.writeBytes, &Prev::writeBytes);

        prev[r.key] = {ticks, s.readBytes, s.writeBytes};
        live[r.key] = std::move(r);
    }

    // Anything live last time and gone now becomes a tombstone (PRC-09).
    for (auto& [key, rec] : live_) {
        if (live.contains(key) || tombstones_.contains(key)) continue;
        rec.exitedAt = ctx.mono;
        tombstones_[key] = std::move(rec);
    }
    for (auto it = tombstones_.begin(); it != tombstones_.end();) {
        if (ctx.mono - *it->second.exitedAt > opts_.tombstoneRetention) {
            it = tombstones_.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& [key, rec] : live) out.processes.processes.push_back(rec);
    for (const auto& [key, rec] : tombstones_) out.processes.processes.push_back(rec);
    out.processes.count = Metric::measured(static_cast<double>(live.size()), Unit::Count);
    out.processes.threadCount = Metric::measured(threadSum, Unit::Count);

    statusCache_.clear();
    for (auto& s : scanned) statusCache_[{s.stat.pid, s.stat.startTicks}] = std::move(s.status);
    live_ = std::move(live);
    prev_ = std::move(prev);
    prevTime_ = ctx.mono;
}

void ProcessProvider::fillUnavailable(Frame& out, Availability why) const {
    out.processes = {};
    out.processes.count = out.processes.threadCount = Metric::notMeasured(why, Unit::Count);
}

}  // namespace pmp::linux
