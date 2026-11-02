// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/history_ring.hpp>

#include <chrono>
#include <optional>
#include <vector>

namespace pmp::vm {

struct Point {
    double x, y;
};
struct PlotSize {
    double w, h;
};
struct Scale {
    double lo, hi;
};
struct Trace {
    std::vector<std::vector<Point>> runs;  // each run is one continuous line; gaps separate runs
};

// x: newest sample at the right edge, so repainting continuously scrolls smoothly.
// y: `scale.lo` at the bottom, `scale.hi` at the top, clamped to the plot.
// A gap slot ends the current run (INT-03); a series with nothing measured in
// the window yields no runs at all (never a flat line at zero).
Trace buildTrace(const HistoryRing<double>& ring, MonoTime now, std::chrono::milliseconds window, PlotSize size,
                 Scale scale);

// lo = 0, hi = max(minSpan, 1.1 x largest visible value).
Scale autoScale(const HistoryRing<double>& ring, MonoTime now, std::chrono::milliseconds window, double minSpan);

// How far (in pixels) a scrolling graph must have moved before it is worth repainting.
// Half a pixel at 60 Hz or more; coarser as the animation frame rate drops.
double scrollQuantumPx(int frameRateHz);

// Hover and range selection (HIS-02/03). The mapping is exactly the one buildTrace paints with: the newest sample
// is at the right edge, `window` wide, so a pointer position and a stored absolute time always agree.
std::optional<MonoTime> timeAtX(double x, PlotSize size, MonoTime now, std::chrono::milliseconds window);  // nullopt outside the plot
std::optional<double> xAtTime(MonoTime t, PlotSize size, MonoTime now, std::chrono::milliseconds window);  // nullopt outside the window

struct HoverSample {
    MonoTime t;
    std::optional<double> value;  // nullopt = a gap: shown as a dash, never as zero
};
// The slot nearest to `t`, if one lies within `tolerance`.
std::optional<HoverSample> sampleNear(const HistoryRing<double>& ring, MonoTime t, std::chrono::milliseconds tolerance);

struct TimeRange {
    MonoTime from, to;
};
TimeRange orderedRange(MonoTime a, MonoTime b);
// The part of `r` inside [now - window, now]; nullopt when none of it is.
std::optional<TimeRange> clipToWindow(TimeRange r, MonoTime now, std::chrono::milliseconds window);

}  // namespace pmp::vm
