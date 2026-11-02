// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <vector>

namespace pmp::vm {

// How busy each channel of the machine is, 0..1. A value that is not measured counts as idle.
struct ChannelLevels {
    double cpu = 0, memory = 0, gpu = 0, disk = 0;
};
ChannelLevels channelLevels(const Frame& f);

// One ring of the F12 system-load tunnel. `scale` runs 0 (centre) to 1 (frame edge); `channel` is
// 0 cpu, 1 memory, 2 gpu, 3 disk; `alpha` is 0..1. A busier channel moves faster and glows brighter, and an
// idle machine is dim but still moving.
struct TunnelRing {
    double scale;
    int channel;
    double alpha;
};
std::vector<TunnelRing> tunnelRings(double timeSeconds, const ChannelLevels& levels, int ringCount = 24);

}  // namespace pmp::vm
