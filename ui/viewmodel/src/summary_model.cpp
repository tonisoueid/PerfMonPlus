// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/summary_model.hpp>

#include <perfmonplus/core/format.hpp>

#include <algorithm>

namespace pmp::vm {
namespace {

Metric unsupported(Unit u) { return Metric::notMeasured(Availability::Unsupported, u); }

VfdColumn column(std::string label, const Metric& m, double fullScale) {
    VfdColumn c;
    c.label = std::move(label);
    c.value = format(m);
    c.measured = m.isMeasured() && m.value;
    c.fraction = c.measured ? std::clamp(*m.value / fullScale, 0.0, 1.0) : 0.0;
    return c;
}

}  // namespace

std::array<VfdColumn, 4> vfdStack(const Frame& f, double clockFullScaleHz) {
    const Metric gpu = f.gpus.empty() ? unsupported(Unit::Percent) : f.gpus.front().util;
    return {column("CPU", f.cpu.total, 100.0), column("Clock", f.cpu.freqHz, clockFullScaleHz),
            column("Temp", f.thermals.hotspot, 110.0), column("GPU", gpu, 100.0)};
}

std::vector<TopProcessRow> topProcesses(const Frame& f, std::size_t n) {
    std::vector<const ProcessRecord*> live;
    for (const auto& p : f.processes.processes) {
        if (!p.exitedAt) live.push_back(&p);
    }
    std::stable_sort(live.begin(), live.end(), [](const ProcessRecord* a, const ProcessRecord* b) {
        const bool am = a->cpu.isMeasured(), bm = b->cpu.isMeasured();
        if (am != bm) return am;
        return am && *a->cpu.value > *b->cpu.value;
    });
    std::vector<TopProcessRow> rows;
    for (std::size_t i = 0; i < live.size() && i < n; ++i) {
        const auto& p = *live[i];
        rows.push_back({p.key, p.key.pid, p.name, format(p.cpu), format(p.gpu), format(p.memFootprint)});
    }
    return rows;
}

std::string memoryHeader(const Frame& f) {
    if (!f.memory.inUse.isMeasured() || !f.memory.total.isMeasured()) return "Unavailable";
    return format(f.memory.inUse) + " / " + format(f.memory.total);
}

TileText diskTile(const Frame& f) {
    const auto& d = f.disks.combined;
    return {"Disks", format(d.activePct), "R: " + format(d.readBps) + "  W: " + format(d.writeBps), d.activePct.isMeasured()};
}

TileText networkTile(const Frame& f) {
    const auto& n = f.net.combined;
    Metric total = n.rxBps.isMeasured() && n.txBps.isMeasured()
                       ? Metric::measured(*n.rxBps.value + *n.txBps.value, Unit::BytesPerSec)
                       : Metric::notMeasured(n.rxBps.isMeasured() ? n.txBps.availability : n.rxBps.availability,
                                             Unit::BytesPerSec);
    return {"Network", format(total), "R: " + format(n.rxBps) + "  S: " + format(n.txBps), total.isMeasured()};
}

}  // namespace pmp::vm
