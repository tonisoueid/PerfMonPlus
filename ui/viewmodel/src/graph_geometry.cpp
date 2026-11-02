// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/graph_geometry.hpp>

#include <algorithm>

namespace pmp::vm {

Trace buildTrace(const HistoryRing<double>& ring, MonoTime now, std::chrono::milliseconds window, PlotSize size,
                 Scale scale) {
    Trace trace;
    if (scale.hi <= scale.lo) scale.hi = scale.lo + 1.0;
    const double windowSec = std::chrono::duration<double>(window).count();
    std::vector<Point> run;

    const auto closeRun = [&] {
        if (!run.empty()) trace.runs.push_back(std::move(run));
        run.clear();
    };

    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto& slot = ring.at(i);
        const double ageSec = std::chrono::duration<double>(now - slot.t).count();
        if (ageSec > windowSec) continue;
        if (!slot.value) {
            closeRun();
            continue;
        }
        const double x = size.w - ageSec / windowSec * size.w;
        const double y = size.h - (*slot.value - scale.lo) / (scale.hi - scale.lo) * size.h;
        run.push_back({x, std::clamp(y, 0.0, size.h)});
    }
    closeRun();
    return trace;
}

Scale autoScale(const HistoryRing<double>& ring, MonoTime now, std::chrono::milliseconds window, double minSpan) {
    double peak = 0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto& slot = ring.at(i);
        if (slot.value && now - slot.t <= window) peak = std::max(peak, *slot.value);
    }
    return {0.0, std::max(minSpan, peak * 1.1)};
}

std::optional<MonoTime> timeAtX(double x, PlotSize size, MonoTime now, std::chrono::milliseconds window) {
    if (size.w <= 0 || x < 0 || x > size.w) return std::nullopt;
    const double ageSec = (size.w - x) / size.w * std::chrono::duration<double>(window).count();
    return now - std::chrono::duration_cast<MonoTime::duration>(std::chrono::duration<double>(ageSec));
}

std::optional<double> xAtTime(MonoTime t, PlotSize size, MonoTime now, std::chrono::milliseconds window) {
    const double ageSec = std::chrono::duration<double>(now - t).count();
    const double windowSec = std::chrono::duration<double>(window).count();
    if (ageSec < 0 || ageSec > windowSec || windowSec <= 0) return std::nullopt;
    return size.w - ageSec / windowSec * size.w;
}

std::optional<HoverSample> sampleNear(const HistoryRing<double>& ring, MonoTime t, std::chrono::milliseconds tolerance) {
    std::optional<HoverSample> best;
    std::chrono::nanoseconds bestDistance{};
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto& slot = ring.at(i);
        auto d = slot.t - t;
        if (d < std::chrono::nanoseconds{0}) d = -d;
        if (d > tolerance) continue;
        if (!best || d < bestDistance) {
            best = HoverSample{slot.t, slot.value};
            bestDistance = d;
        }
    }
    return best;
}

TimeRange orderedRange(MonoTime a, MonoTime b) { return a <= b ? TimeRange{a, b} : TimeRange{b, a}; }

std::optional<TimeRange> clipToWindow(TimeRange r, MonoTime now, std::chrono::milliseconds window) {
    const MonoTime lo = now - window;
    if (r.to < lo || r.from > now) return std::nullopt;
    return TimeRange{std::max(r.from, lo), std::min(r.to, now)};
}

double scrollQuantumPx(int frameRateHz) {
    if (frameRateHz >= 60) return 0.5;
    if (frameRateHz >= 30) return 1.0;
    return 2.0;
}

}  // namespace pmp::vm
