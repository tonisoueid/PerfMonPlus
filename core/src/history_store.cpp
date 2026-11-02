// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_store.hpp>

#include <algorithm>
#include <stdexcept>

namespace pmp {

HistoryStore::HistoryStore(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) throw std::invalid_argument("HistoryStore capacity must be > 0");
    for (std::size_t i = 0; i < static_cast<std::size_t>(Series::Count_); ++i) series_.emplace_back(capacity);
}

void HistoryStore::reset() {
    for (auto& r : series_) r.clear();
    for (auto& r : logical_) r.clear();
    for (auto& r : logicalFreq_) r.clear();
    for (auto& r : gpu_) r.clear();
    for (auto& r : npu_) r.clear();
    for (auto& [id, r] : sensors_) r.clear();
    for (auto& [id, r] : domains_) r.clear();
}

void HistoryStore::put(HistoryRing<double>& r, const Metric& m, MonoTime t, bool gapBefore) {
    if (gapBefore) r.pushGap(t);
    if (m.isMeasured() && m.value) {
        r.push(t, *m.value);
    } else {
        r.pushGap(t);
    }
}

void HistoryStore::ingest(const Frame& f, bool gapBefore) {
    const auto at = [&](Series s) -> HistoryRing<double>& { return series_[static_cast<std::size_t>(s)]; };
    put(at(Series::CpuTotal), f.cpu.total, f.mono, gapBefore);
    put(at(Series::CpuKernel), f.cpu.kernel, f.mono, gapBefore);
    put(at(Series::MemoryUsedBytes), f.memory.inUse, f.mono, gapBefore);
    put(at(Series::DiskActivePct), f.disks.combined.activePct, f.mono, gapBefore);
    put(at(Series::DiskReadBps), f.disks.combined.readBps, f.mono, gapBefore);
    put(at(Series::DiskWriteBps), f.disks.combined.writeBps, f.mono, gapBefore);
    put(at(Series::NetRxBps), f.net.combined.rxBps, f.mono, gapBefore);
    put(at(Series::NetTxBps), f.net.combined.txBps, f.mono, gapBefore);
    put(at(Series::PowerW), f.energy.watts, f.mono, gapBefore);
    put(at(Series::HotspotC), f.thermals.hotspot, f.mono, gapBefore);

    while (gpu_.size() < f.gpus.size()) gpu_.emplace_back(capacity_);
    for (std::size_t i = 0; i < gpu_.size(); ++i) put(gpu_[i], i < f.gpus.size() ? f.gpus[i].util : Metric{}, f.mono, gapBefore);
    while (npu_.size() < f.npus.size()) npu_.emplace_back(capacity_);
    for (std::size_t i = 0; i < npu_.size(); ++i) put(npu_[i], i < f.npus.size() ? f.npus[i].activePct : Metric{}, f.mono, gapBefore);
    for (const auto& s : f.thermals.sensors) {
        auto it = sensors_.try_emplace(s.id, capacity_).first;
        put(it->second, s.celsius, f.mono, gapBefore);
    }
    // A sensor that is not in this frame (unplugged, driver reloaded) leaves a gap: when it returns the graph must not
    // draw a line across the hole.
    for (auto& [id, ring] : sensors_) {
        const bool present = std::any_of(f.thermals.sensors.begin(), f.thermals.sensors.end(), [&](const auto& s) { return s.id == id; });
        if (!present) put(ring, Metric{}, f.mono, gapBefore);
    }

    for (const auto& d : f.energy.domains) {
        auto it = domains_.try_emplace(d.id, capacity_).first;
        put(it->second, d.watts, f.mono, gapBefore);
    }
    for (auto& [id, ring] : domains_) {
        const bool present = std::any_of(f.energy.domains.begin(), f.energy.domains.end(), [&](const auto& d) { return d.id == id; });
        if (!present) put(ring, Metric{}, f.mono, gapBefore);
    }

    for (const auto& l : f.cpu.logical) {
        if (std::find(logicalIds_.begin(), logicalIds_.end(), l.id) == logicalIds_.end()) {
            logicalIds_.push_back(l.id);
            logical_.emplace_back(capacity_);
            logicalFreq_.emplace_back(capacity_);
        }
    }
    for (std::size_t i = 0; i < logicalIds_.size(); ++i) {
        const auto it = std::find_if(f.cpu.logical.begin(), f.cpu.logical.end(),
                                     [&](const LogicalCpu& l) { return l.id == logicalIds_[i]; });
        put(logicalFreq_[i], it != f.cpu.logical.end() ? it->freqHz : Metric{}, f.mono, gapBefore);  // offline: a gap
        put(logical_[i], it != f.cpu.logical.end() ? it->util : Metric{}, f.mono, gapBefore);
    }
}

const HistoryRing<double>& HistoryStore::gpuUtil(std::size_t i) const {
    if (i >= gpu_.size()) throw std::out_of_range("gpu index out of range");
    return gpu_[i];
}

const HistoryRing<double>& HistoryStore::npuUtil(std::size_t i) const {
    if (i >= npu_.size()) throw std::out_of_range("npu index out of range");
    return npu_[i];
}

const HistoryRing<double>& HistoryStore::sensor(const std::string& id) const {
    const auto it = sensors_.find(id);
    if (it == sensors_.end()) throw std::out_of_range("unknown sensor: " + id);
    return it->second;
}

const HistoryRing<double>& HistoryStore::logicalFreq(std::size_t i) const {
    if (i >= logicalFreq_.size()) throw std::out_of_range("logical cpu index out of range");
    return logicalFreq_[i];
}

const HistoryRing<double>& HistoryStore::domain(const std::string& id) const {
    const auto it = domains_.find(id);
    if (it == domains_.end()) throw std::out_of_range("unknown power domain: " + id);
    return it->second;
}

std::vector<std::string> HistoryStore::domainIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, ring] : domains_) ids.push_back(id);
    return ids;
}

int HistoryStore::logicalId(std::size_t i) const {
    if (i >= logicalIds_.size()) throw std::out_of_range("logical cpu index out of range");
    return logicalIds_[i];
}

const HistoryRing<double>* HistoryStore::logicalCpuById(int id) const {
    const auto it = std::find(logicalIds_.begin(), logicalIds_.end(), id);
    return it == logicalIds_.end() ? nullptr : &logical_[static_cast<std::size_t>(it - logicalIds_.begin())];
}

const HistoryRing<double>* HistoryStore::logicalFreqById(int id) const {
    const auto it = std::find(logicalIds_.begin(), logicalIds_.end(), id);
    return it == logicalIds_.end() ? nullptr : &logicalFreq_[static_cast<std::size_t>(it - logicalIds_.begin())];
}

const HistoryRing<double>& HistoryStore::logicalCpu(std::size_t i) const {
    if (i >= logical_.size()) throw std::out_of_range("logical cpu index out of range");
    return logical_[i];
}

}  // namespace pmp
