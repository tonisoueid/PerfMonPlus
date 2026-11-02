// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/core/history_ring.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pmp {

// A compact history ring of CPU, memory and disk for every live process (DM-05: 48 bytes per process per sample),
// kept for exited processes until their newest sample leaves the window, so "who caused this spike?" (HIS-03) can
// name processes that have since died. A reading that is not measured is a gap, never zero (INT-03).
class ProcessHistory {
public:
    explicit ProcessHistory(std::size_t capacity = 300, std::size_t maxTracked = 3000);

    void ingest(const Frame& f, bool gapBefore);

    // Copies for graphs (HistoryGraph borrows HistoryRing<double>); nullopt for an unknown key.
    std::optional<HistoryRing<double>> cpuSeries(ProcessKey key) const;
    std::optional<HistoryRing<double>> memorySeries(ProcessKey key) const;  // footprint, bytes
    std::optional<HistoryRing<double>> diskSeries(ProcessKey key) const;    // read + write, bytes/s

    std::size_t tracked() const { return entries_.size(); }
    // The oldest sample still kept for any process: how far back a culprit lookup can reach.
    std::optional<MonoTime> coverageStart() const;
    std::size_t approxBytes() const;
    void clear() { entries_.clear(); }

    enum class Resource { Cpu, Memory };
    struct Culprit {
        ProcessKey key;
        std::string name;
        bool exited = false;
        double cpuSeconds = 0, avgCpuPct = 0, avgMemBytes = 0, peakMemBytes = 0;
    };
    // Processes with data in [from, to], ranked by integrated CPU-seconds (Cpu) or by peak footprint (Memory).
    std::vector<Culprit> culprits(MonoTime from, MonoTime to, Resource by, std::size_t logicalCpus,
                                  std::size_t limit = 10) const;

private:
    struct Entry {
        explicit Entry(std::size_t capacity) : cpu(capacity), memory(capacity), disk(capacity) {}
        std::string name;
        HistoryRing<float> cpu, memory, disk;
        std::uint64_t lastSeenIngest = 0;
        bool exited = false;
        double lastCpu = -1;  // for choosing who to drop when the tracked count is capped
    };
    static void put(HistoryRing<float>& r, const Metric& m, MonoTime t, bool gapBefore);
    static std::optional<HistoryRing<double>> widen(const HistoryRing<float>& r);

    std::size_t capacity_, maxTracked_;
    std::uint64_t ingests_ = 0;
    std::map<ProcessKey, Entry> entries_;
};

}  // namespace pmp
