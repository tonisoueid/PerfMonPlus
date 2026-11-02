// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/tunnel.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::vm {
namespace {

double unit(const Metric& m, double full) { return m.isMeasured() && full > 0 ? std::clamp(*m.value / full, 0.0, 1.0) : 0.0; }

double levelOf(const ChannelLevels& l, int channel) {
    switch (channel) {
        case 0: return l.cpu;
        case 1: return l.memory;
        case 2: return l.gpu;
        default: return l.disk;
    }
}

}  // namespace

ChannelLevels channelLevels(const Frame& f) {
    ChannelLevels l;
    l.cpu = unit(f.cpu.total, 100);
    if (f.memory.total.isMeasured() && f.memory.inUse.isMeasured()) l.memory = unit(f.memory.inUse, *f.memory.total.value);
    for (const auto& g : f.gpus) l.gpu = std::max(l.gpu, unit(g.util, 100));
    l.disk = unit(f.disks.combined.activePct, 100);
    return l;
}

std::vector<TunnelRing> tunnelRings(double t, const ChannelLevels& levels, int ringCount) {
    std::vector<TunnelRing> rings;
    for (int i = 0; i < ringCount; ++i) {
        const int channel = i % 4;
        const double level = std::clamp(levelOf(levels, channel), 0.0, 1.0);
        const double x = static_cast<double>(i) / ringCount + t * (0.15 + 0.35 * level);
        const double p = x - std::floor(x);
        rings.push_back({p * p, channel, (0.15 + 0.85 * level) * p});
    }
    return rings;
}

}  // namespace pmp::vm
