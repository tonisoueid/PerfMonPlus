// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include "frame_json.hpp"

#include <algorithm>
#include <vector>

namespace pmp::probe {
namespace {

using nlohmann::json;

std::string_view toString(CoreClass c) {
    switch (c) {
        case CoreClass::Performance: return "performance";
        case CoreClass::Efficiency: return "efficiency";
        case CoreClass::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view toString(DiskType t) {
    switch (t) {
        case DiskType::SSD: return "ssd";
        case DiskType::HDD: return "hdd";
        case DiskType::Mixed: return "mixed";
        case DiskType::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view toString(HealthState s) {
    switch (s) {
        case HealthState::Healthy: return "Healthy";
        case HealthState::Degraded: return "Degraded";
        case HealthState::Failed: return "Failed";
        case HealthState::Disabled: return "Disabled";
    }
    return "Failed";
}

json diskToJson(const DiskSnapshot& d) {
    return {{"name", d.name},
            {"model", d.model},
            {"type", toString(d.type)},
            {"read_bps", metricToJson(d.readBps)},
            {"write_bps", metricToJson(d.writeBps)},
            {"active_pct", metricToJson(d.activePct)},
            {"avg_response_ms", metricToJson(d.avgResponseMs)},
            {"total_read", metricToJson(d.totalRead)},
            {"total_written", metricToJson(d.totalWritten)},
            {"capacity", metricToJson(d.capacity)}};
}

json netToJson(const NetInterface& n) {
    return {{"name", n.name},
            {"virtual", n.isVirtual},
            {"up", n.up},
            {"rx_bps", metricToJson(n.rxBps)},
            {"tx_bps", metricToJson(n.txBps)},
            {"total_rx", metricToJson(n.totalRx)},
            {"total_tx", metricToJson(n.totalTx)}};
}

bool cpuLess(const ProcessRecord* a, const ProcessRecord* b) {
    const bool am = a->cpu.isMeasured(), bm = b->cpu.isMeasured();
    if (am != bm) return am;  // pending/unavailable sorts last
    if (am && *a->cpu.value != *b->cpu.value) return *a->cpu.value > *b->cpu.value;
    return a->key < b->key;
}

}  // namespace

json metricToJson(const Metric& m) {
    json j;
    j["value"] = m.value ? json(*m.value) : json(nullptr);
    j["availability"] = toString(m.availability);
    j["unit"] = toString(m.unit);
    return j;
}

json toJson(const Frame& f, std::size_t topN, bool gapBefore) {
    json j;
    j["generation"] = f.generation;
    j["wall_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(f.wall.time_since_epoch()).count();
    j["gap_before"] = gapBefore;

    json logical = json::array();
    for (const auto& c : f.cpu.logical) {
        logical.push_back({{"id", c.id},
                           {"core_class", toString(c.coreClass)},
                           {"util", metricToJson(c.util)},
                           {"kernel", metricToJson(c.kernel)},
                           {"freq_hz", metricToJson(c.freqHz)},
                           {"freq_min_hz", metricToJson(c.freqMinHz)},
                           {"freq_max_hz", metricToJson(c.freqMaxHz)}});
    }
    j["cpu"] = {{"total", metricToJson(f.cpu.total)},
                {"kernel", metricToJson(f.cpu.kernel)},
                {"uptime", metricToJson(f.cpu.uptime)},
                {"freq_hz", metricToJson(f.cpu.freqHz)},
                {"throttle_events", metricToJson(f.cpu.throttleEvents)},
                {"driver", f.cpu.driver},
                {"governor", f.cpu.governor},
                {"epp", f.cpu.epp},
                {"logical", logical}};

    const auto& m = f.memory;
    j["memory"] = {{"total", metricToJson(m.total)},
                   {"in_use", metricToJson(m.inUse)},
                   {"available", metricToJson(m.available)},
                   {"committed", metricToJson(m.committed)},
                   {"cached", metricToJson(m.cached)},
                   {"buffers", metricToJson(m.buffers)},
                   {"swap_total", metricToJson(m.swapTotal)},
                   {"swap_used", metricToJson(m.swapUsed)},
                   {"swap_available", metricToJson(m.swapAvailable)},
                   {"compressed", metricToJson(m.compressed)},
                   {"pressure_some_avg10", metricToJson(m.pressureSomeAvg10)},
                   {"page_ins", metricToJson(m.pageIns)},
                   {"page_outs", metricToJson(m.pageOuts)}};

    json devices = json::array();
    for (const auto& d : f.disks.devices) devices.push_back(diskToJson(d));
    j["disks"] = {{"devices", devices}, {"combined", diskToJson(f.disks.combined)}};

    json ifaces = json::array();
    for (const auto& n : f.net.interfaces) ifaces.push_back(netToJson(n));
    j["network"] = {{"interfaces", ifaces}, {"combined", netToJson(f.net.combined)}};

    std::vector<const ProcessRecord*> live;
    for (const auto& p : f.processes.processes) {
        if (!p.exitedAt) live.push_back(&p);
    }
    std::sort(live.begin(), live.end(), cpuLess);
    json top = json::array();
    for (std::size_t i = 0; i < live.size() && i < topN; ++i) {
        const auto& p = *live[i];
        top.push_back({{"pid", p.key.pid},
                       {"start_ticks", p.key.startTicks},
                       {"name", p.name},
                       {"user", p.user},
                       {"cpu", metricToJson(p.cpu)},
                       {"memory", metricToJson(p.memFootprint)}});
    }
    j["processes"] = {{"count", metricToJson(f.processes.count)},
                      {"thread_count", metricToJson(f.processes.threadCount)},
                      {"top", top}};

    json health = json::array();
    for (const auto& h : f.providerHealth) {
        health.push_back({{"id", h.id},
                          {"state", toString(h.state)},
                          {"last_error", h.lastError},
                          {"consecutive_failures", h.consecutiveFailures},
                          {"avg_sample_cost_us", h.avgSampleCostMicros}});
    }
    j["provider_health"] = health;
    return j;
}

std::string dumpLine(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace pmp::probe
