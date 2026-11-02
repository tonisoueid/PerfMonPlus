// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/disk_provider.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <set>

namespace pmp::linux {
namespace {

constexpr double kSector = 512.0;

bool isPhysicalName(const std::string& n) {
    for (const char* skip : {"loop", "ram", "zram", "dm-"}) {
        if (n.starts_with(skip)) return false;
    }
    return true;
}

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

std::optional<std::uint64_t> readUint(const std::filesystem::path& p) {
    const auto f = readText(p);
    std::uint64_t v = 0;
    if (!f.ok() || std::from_chars(f.text.data(), f.text.data() + f.text.size(), v).ec != std::errc{}) {
        return std::nullopt;
    }
    return v;
}

Metric pending(Unit u) { return Metric::notMeasured(Availability::Pending, u); }
Metric unsupported(Unit u) { return Metric::notMeasured(Availability::Unsupported, u); }

}  // namespace

void DiskProvider::sample(Frame& out, const SampleContext& ctx) {
    auto& disks = out.disks;
    disks = {};

    const auto stats = readText(root_.proc / "diskstats");
    if (!stats.ok()) {
        fillUnavailable(out, availabilityForErrno(stats.error));
        return;
    }
    std::map<std::string, Stats, std::less<>> now;
    {
        std::string_view text = stats.text;
        while (!text.empty()) {
            const auto nl = text.find('\n');
            std::string_view line = text.substr(0, nl);
            text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
            std::vector<std::string_view> tok;
            while (!line.empty()) {
                while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
                const auto sp = line.find(' ');
                if (!line.empty()) tok.push_back(line.substr(0, sp));
                line = sp == std::string_view::npos ? std::string_view{} : line.substr(sp);
            }
            if (tok.size() < 14) continue;
            std::uint64_t f[11] = {};
            bool ok = true;
            for (int i = 0; i < 11 && ok; ++i) {
                const auto t = tok[3 + static_cast<std::size_t>(i)];
                ok = std::from_chars(t.data(), t.data() + t.size(), f[i]).ec == std::errc{};
            }
            if (!ok) continue;
            now[std::string(tok[2])] = {f[0], f[2], f[3], f[4], f[6], f[7], f[9]};
        }
    }

    std::set<std::string> names;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(root_.sys / "block", ec)) {
        const auto name = e.path().filename().string();
        std::error_code existsEc;
        if (isPhysicalName(name) && std::filesystem::exists(e.path() / "device", existsEc)) names.insert(name);
    }
    if (ec) {
        fillUnavailable(out, ec == std::errc::no_such_file_or_directory ? Availability::Unsupported
                                                                        : Availability::Failed);
        return;
    }

    const double dt = prevTime_ ? std::chrono::duration<double>(ctx.mono - *prevTime_).count() : 0.0;

    struct Delta {
        double ms = 0, ops = 0;
    };
    Delta sum;
    bool anyPending = false;
    bool anyMissing = false;
    std::set<DiskType> types;
    double rSum = 0, wSum = 0, activeMax = 0, readTot = 0, writeTot = 0, capTot = 0;
    bool capKnown = true;

    for (const auto& name : names) {
        DiskSnapshot d;
        d.name = name;
        const auto base = root_.sys / "block" / name;
        d.model = trimmed(readText(base / "device/model").text);
        const auto rot = readUint(base / "queue/rotational");
        d.type = !rot ? DiskType::Unknown : (*rot ? DiskType::HDD : DiskType::SSD);
        types.insert(d.type);
        if (const auto sectors = readUint(base / "size")) {
            d.capacity = Metric::measured(static_cast<double>(*sectors) * kSector, Unit::Bytes);
            capTot += *d.capacity.value;
        } else {
            d.capacity = unsupported(Unit::Bytes);
            capKnown = false;
        }

        const auto cur = now.find(name);
        if (cur == now.end()) {
            d.readBps = d.writeBps = unsupported(Unit::BytesPerSec);
            d.activePct = unsupported(Unit::Percent);
            d.avgResponseMs = unsupported(Unit::Milliseconds);
            d.totalRead = d.totalWritten = unsupported(Unit::Bytes);
            anyMissing = true;
        } else {
            const Stats& c = cur->second;
            d.totalRead = Metric::measured(static_cast<double>(c.sectorsRead) * kSector, Unit::Bytes);
            d.totalWritten = Metric::measured(static_cast<double>(c.sectorsWritten) * kSector, Unit::Bytes);
            readTot += *d.totalRead.value;
            writeTot += *d.totalWritten.value;

            const auto p = prev_.find(name);
            const bool forward = p != prev_.end() && dt > 0 && c.reads >= p->second.reads &&
                                 c.sectorsRead >= p->second.sectorsRead && c.msReading >= p->second.msReading &&
                                 c.writes >= p->second.writes && c.sectorsWritten >= p->second.sectorsWritten &&
                                 c.msWriting >= p->second.msWriting && c.msIo >= p->second.msIo;
            if (!forward) {
                d.readBps = d.writeBps = pending(Unit::BytesPerSec);
                d.activePct = pending(Unit::Percent);
                d.avgResponseMs = pending(Unit::Milliseconds);
                anyPending = true;
            } else {
                const Stats& o = p->second;
                const double rb = static_cast<double>(c.sectorsRead - o.sectorsRead) * kSector / dt;
                const double wb = static_cast<double>(c.sectorsWritten - o.sectorsWritten) * kSector / dt;
                const double active =
                    std::min(100.0, static_cast<double>(c.msIo - o.msIo) / (dt * 1000.0) * 100.0);
                const double ms = static_cast<double>((c.msReading - o.msReading) + (c.msWriting - o.msWriting));
                const double ops = static_cast<double>((c.reads - o.reads) + (c.writes - o.writes));
                d.readBps = Metric::measured(rb, Unit::BytesPerSec);
                d.writeBps = Metric::measured(wb, Unit::BytesPerSec);
                d.activePct = Metric::measured(active, Unit::Percent);
                d.avgResponseMs = Metric::measured(ops > 0 ? ms / ops : 0.0, Unit::Milliseconds);
                rSum += rb;
                wSum += wb;
                activeMax = std::max(activeMax, active);
                sum.ms += ms;
                sum.ops += ops;
            }
        }
        disks.devices.push_back(std::move(d));
    }

    auto& c = disks.combined;
    c.name = "combined";
    c.type = types.empty() ? DiskType::Unknown : types.size() == 1 ? *types.begin() : DiskType::Mixed;
    c.capacity = capKnown && !names.empty() ? Metric::measured(capTot, Unit::Bytes) : unsupported(Unit::Bytes);
    if (names.empty() || anyMissing) {
        // Partial aggregates would present one disk's numbers as the whole.
        c.totalRead = c.totalWritten = unsupported(Unit::Bytes);
        c.readBps = c.writeBps = unsupported(Unit::BytesPerSec);
        c.activePct = unsupported(Unit::Percent);
        c.avgResponseMs = unsupported(Unit::Milliseconds);
    } else {
        c.totalRead = Metric::measured(readTot, Unit::Bytes);
        c.totalWritten = Metric::measured(writeTot, Unit::Bytes);
    }
    if (names.empty() || anyMissing) {
        // already Unsupported above
    } else if (anyPending) {
        c.readBps = c.writeBps = pending(Unit::BytesPerSec);
        c.activePct = pending(Unit::Percent);
        c.avgResponseMs = pending(Unit::Milliseconds);
    } else {
        c.readBps = Metric::measured(rSum, Unit::BytesPerSec);
        c.writeBps = Metric::measured(wSum, Unit::BytesPerSec);
        c.activePct = Metric::measured(activeMax, Unit::Percent);
        c.avgResponseMs = Metric::measured(sum.ops > 0 ? sum.ms / sum.ops : 0.0, Unit::Milliseconds);
    }

    prev_ = std::move(now);
    prevTime_ = ctx.mono;
}

void DiskProvider::fillUnavailable(Frame& out, Availability why) const {
    out.disks = {};
    auto& c = out.disks.combined;
    c.name = "combined";
    c.readBps = c.writeBps = Metric::notMeasured(why, Unit::BytesPerSec);
    c.activePct = Metric::notMeasured(why, Unit::Percent);
    c.avgResponseMs = Metric::notMeasured(why, Unit::Milliseconds);
    c.totalRead = c.totalWritten = c.capacity = Metric::notMeasured(why, Unit::Bytes);
}

}  // namespace pmp::linux
