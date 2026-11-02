// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/process_history.hpp>

#include <algorithm>

namespace pmp {

ProcessHistory::ProcessHistory(std::size_t capacity, std::size_t maxTracked)
    : capacity_(capacity), maxTracked_(maxTracked) {}

void ProcessHistory::put(HistoryRing<float>& r, const Metric& m, MonoTime t, bool gapBefore) {
    if (gapBefore) r.pushGap(t);
    if (m.isMeasured() && m.value) {
        r.push(t, static_cast<float>(*m.value));
    } else {
        r.pushGap(t);
    }
}

void ProcessHistory::ingest(const Frame& f, bool gapBefore) {
    ++ingests_;
    std::vector<const ProcessRecord*> fresh;  // live processes not tracked yet
    for (const auto& r : f.processes.processes) {
        if (r.exitedAt) continue;
        const auto it = entries_.find(r.key);
        if (it == entries_.end()) {
            fresh.push_back(&r);
            continue;
        }
        auto& e = it->second;
        e.lastSeenIngest = ingests_;
        e.exited = false;
        e.lastCpu = r.cpu.isMeasured() ? *r.cpu.value : -1;
        put(e.cpu, r.cpu, f.mono, gapBefore);
        put(e.memory, r.memFootprint, f.mono, gapBefore);
        Metric disk = Metric::notMeasured(Availability::Pending, Unit::BytesPerSec);
        if (r.diskReadBps.isMeasured() && r.diskWriteBps.isMeasured()) {
            disk = Metric::measured(*r.diskReadBps.value + *r.diskWriteBps.value, Unit::BytesPerSec);
        }
        put(e.disk, disk, f.mono, gapBefore);
    }

    // Anything we track that was not live in this frame has exited; forget it once it is older than the window.
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& e = it->second;
        if (e.lastSeenIngest != ingests_) {
            e.exited = true;
            if (ingests_ - e.lastSeenIngest >= capacity_) {
                it = entries_.erase(it);
                continue;
            }
        }
        ++it;
    }

    // New processes, busiest first, so a cap keeps the ones that matter.
    std::sort(fresh.begin(), fresh.end(), [](const ProcessRecord* a, const ProcessRecord* b) {
        const double ca = a->cpu.isMeasured() ? *a->cpu.value : -1, cb = b->cpu.isMeasured() ? *b->cpu.value : -1;
        return ca > cb;
    });
    for (const auto* r : fresh) {
        const double cpu = r->cpu.isMeasured() ? *r->cpu.value : -1;
        if (entries_.size() >= maxTracked_) {
            // Drop an exited entry first, otherwise the least busy live one, but only for a busier newcomer.
            auto victim = entries_.end();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                const bool better = victim == entries_.end() || (it->second.exited && !victim->second.exited) ||
                                    (it->second.exited == victim->second.exited && it->second.lastCpu < victim->second.lastCpu);
                if (better) victim = it;
            }
            if (victim == entries_.end() || (!victim->second.exited && victim->second.lastCpu >= cpu)) break;
            entries_.erase(victim);
        }
        auto [it, inserted] = entries_.try_emplace(r->key, capacity_);
        auto& e = it->second;
        e.name = r->name;
        e.lastSeenIngest = ingests_;
        e.lastCpu = cpu;
        put(e.cpu, r->cpu, f.mono, gapBefore);
        put(e.memory, r->memFootprint, f.mono, gapBefore);
        Metric disk = Metric::notMeasured(Availability::Pending, Unit::BytesPerSec);
        if (r->diskReadBps.isMeasured() && r->diskWriteBps.isMeasured()) {
            disk = Metric::measured(*r->diskReadBps.value + *r->diskWriteBps.value, Unit::BytesPerSec);
        }
        put(e.disk, disk, f.mono, gapBefore);
    }
    // Names of already-tracked processes can change (exec); keep the latest.
    for (const auto& r : f.processes.processes) {
        if (r.exitedAt) continue;
        if (const auto it = entries_.find(r.key); it != entries_.end()) it->second.name = r.name;
    }
}

std::optional<HistoryRing<double>> ProcessHistory::widen(const HistoryRing<float>& r) {
    HistoryRing<double> out(r.capacity());
    for (std::size_t i = 0; i < r.size(); ++i) {
        const auto& s = r.at(i);
        if (s.value) out.push(s.t, static_cast<double>(*s.value));
        else out.pushGap(s.t);
    }
    return out;
}

std::optional<HistoryRing<double>> ProcessHistory::cpuSeries(ProcessKey key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? std::nullopt : widen(it->second.cpu);
}
std::optional<HistoryRing<double>> ProcessHistory::memorySeries(ProcessKey key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? std::nullopt : widen(it->second.memory);
}
std::optional<HistoryRing<double>> ProcessHistory::diskSeries(ProcessKey key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? std::nullopt : widen(it->second.disk);
}

std::optional<MonoTime> ProcessHistory::coverageStart() const {
    std::optional<MonoTime> oldest;
    for (const auto& [key, e] : entries_) {
        if (e.cpu.size() == 0) continue;
        const MonoTime t = e.cpu.at(0).t;
        if (!oldest || t < *oldest) oldest = t;
    }
    return oldest;
}

std::size_t ProcessHistory::approxBytes() const {
    std::size_t total = 0;
    for (const auto& [key, e] : entries_) {
        total += 3 * capacity_ * sizeof(HistoryRing<float>::Slot) + sizeof(Entry) + e.name.capacity();
    }
    return total;
}

std::vector<ProcessHistory::Culprit> ProcessHistory::culprits(MonoTime from, MonoTime to, Resource by,
                                                              std::size_t logicalCpus, std::size_t limit) const {
    std::vector<Culprit> out;
    for (const auto& [key, e] : entries_) {
        Culprit c;
        c.key = key;
        c.name = e.name;
        c.exited = e.exited;
        double cpuSum = 0, memSum = 0;
        std::size_t cpuN = 0, memN = 0;
        for (std::size_t i = 0; i < e.cpu.size(); ++i) {
            const auto& s = e.cpu.at(i);
            if (s.t < from || s.t > to || !s.value) continue;
            ++cpuN;
            cpuSum += static_cast<double>(*s.value);
            if (i > 0 && e.cpu.at(i - 1).value) {  // an interval counts only between two measured samples
                const double dt = std::chrono::duration<double>(s.t - e.cpu.at(i - 1).t).count();
                c.cpuSeconds += static_cast<double>(*s.value) / 100.0 * static_cast<double>(logicalCpus) * dt;
            }
        }
        for (std::size_t i = 0; i < e.memory.size(); ++i) {
            const auto& s = e.memory.at(i);
            if (s.t < from || s.t > to || !s.value) continue;
            ++memN;
            memSum += static_cast<double>(*s.value);
            c.peakMemBytes = std::max(c.peakMemBytes, static_cast<double>(*s.value));
        }
        if ((by == Resource::Cpu ? cpuN : memN) == 0) continue;
        c.avgCpuPct = cpuN ? cpuSum / static_cast<double>(cpuN) : 0;
        c.avgMemBytes = memN ? memSum / static_cast<double>(memN) : 0;
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [by](const Culprit& a, const Culprit& b) {
        const double ka = by == Resource::Cpu ? a.cpuSeconds : a.peakMemBytes;
        const double kb = by == Resource::Cpu ? b.cpuSeconds : b.peakMemBytes;
        if (ka != kb) return ka > kb;
        return a.key < b.key;
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

}  // namespace pmp
