// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/core/history_ring.hpp>

#include <array>
#include <deque>
#include <map>
#include <string>
#include <cstddef>
#include <vector>

namespace pmp {

enum class Series {
    CpuTotal,
    CpuKernel,
    MemoryUsedBytes,
    DiskActivePct,
    DiskReadBps,
    DiskWriteBps,
    NetRxBps,
    NetTxBps,
    PowerW,
    HotspotC,
    Count_
};

// Feeds graphs from frames. A reading that is not Measured is stored as a gap,
// never as zero (INT-02/03); `gapBefore` marks a suspend or long stall.
class HistoryStore {
public:
    explicit HistoryStore(std::size_t capacity = 3600);

    void ingest(const Frame& f, bool gapBefore);
    // Empties every ring in place. The rings stay where they are, so graphs that hold pointers to them stay valid.
    void reset();

    const HistoryRing<double>& series(Series s) const { return series_[static_cast<std::size_t>(s)]; }
    std::size_t logicalCount() const noexcept { return logical_.size(); }
    const HistoryRing<double>& logicalCpu(std::size_t i) const;  // i-th core ever seen; out of range -> std::out_of_range
    int logicalId(std::size_t i) const;                          // the CPU id of that ring
    // By CPU id, so a core going offline (it vanishes from /proc/stat) does not shift the others. nullptr when never seen.
    const HistoryRing<double>* logicalCpuById(int id) const;
    const HistoryRing<double>* logicalFreqById(int id) const;
    std::size_t gpuCount() const noexcept { return gpu_.size(); }
    std::size_t npuCount() const noexcept { return npu_.size(); }
    const HistoryRing<double>& gpuUtil(std::size_t i) const;      // out of range -> std::out_of_range
    const HistoryRing<double>& npuUtil(std::size_t i) const;
    const HistoryRing<double>& sensor(const std::string& id) const;  // unknown id -> std::out_of_range
    const HistoryRing<double>& logicalFreq(std::size_t i) const;      // out of range -> std::out_of_range
    const HistoryRing<double>& domain(const std::string& id) const;   // unknown id -> std::out_of_range
    std::vector<std::string> domainIds() const;

private:
    static void put(HistoryRing<double>& r, const Metric& m, MonoTime t, bool gapBefore);

    std::size_t capacity_;
    std::vector<HistoryRing<double>> series_;
    // Deques: graphs hold pointers to these, and growing must not move the rings that already exist.
    std::deque<HistoryRing<double>> logical_, logicalFreq_;
    std::vector<int> logicalIds_;  // CPU id of logical_[i] / logicalFreq_[i], in first-seen order
    // deque / map: references stay valid when new devices or sensors appear
    std::deque<HistoryRing<double>> gpu_, npu_;
    std::map<std::string, HistoryRing<double>> sensors_, domains_;
};

}  // namespace pmp
