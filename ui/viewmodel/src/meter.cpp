// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/meter.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::vm {
namespace {

Rgb lerp(Rgb a, Rgb b, double t) {
    const auto ch = [&](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(x + (y - x) * t), 0L, 255L));
    };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

}  // namespace

int litSegments(const Metric& m, double fullScale, int segments) {
    if (!m.isMeasured() || !m.value || fullScale <= 0 || segments <= 0) return 0;
    if (*m.value <= 0) return 0;
    const int lit = static_cast<int>(std::ceil(*m.value / fullScale * segments));
    return std::clamp(lit, 1, segments);
}

Rgb segmentColor(const Theme& t, MeterKind kind, int index, int segments) {
    switch (kind) {
        case MeterKind::Cpu: {
            if (t.phosphor || segments < 2) return t.cpu;
            const double p = std::clamp(static_cast<double>(index) / (segments - 1), 0.0, 1.0);
            constexpr Rgb yellow{0xf2, 0xd3, 0x38};
            constexpr Rgb red{0xe5, 0x48, 0x4d};
            return p < 0.6 ? lerp(t.cpu, yellow, p / 0.6) : lerp(yellow, red, (p - 0.6) / 0.4);
        }
        case MeterKind::Memory: return t.memory;
        case MeterKind::Disk: return t.disk;
        case MeterKind::Network:
        case MeterKind::Gpu: return t.netRx;
        case MeterKind::Energy: return t.energy;
        case MeterKind::Thermal: return t.temperature;
    }
    return t.cpu;
}

double easeToward(double current, double target, double dtSeconds, double tauSeconds) {
    if (dtSeconds <= 0 || tauSeconds <= 0) return current;
    return current + (target - current) * (1.0 - std::exp(-dtSeconds / tauSeconds));
}

}  // namespace pmp::vm
