// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>
#include <perfmonplus/vm/theme.hpp>

namespace pmp::vm {

enum class MeterKind { Cpu, Memory, Disk, Network, Gpu, Energy, Thermal };

// Segments lit for a reading. Not measured -> 0 (never a lit bar); measured 0 -> 0;
// any positive value lights at least one; overflow clamps to `segments`.
int litSegments(const Metric& m, double fullScale, int segments);

// CPU: green -> yellow -> red across the bar (single hue on phosphor themes);
// every other kind uses its theme colour.
Rgb segmentColor(const Theme& t, MeterKind kind, int index, int segments);

// Exponential approach; never overshoots; dt == 0 leaves the value unchanged.
double easeToward(double current, double target, double dtSeconds, double tauSeconds = 0.12);

}  // namespace pmp::vm
