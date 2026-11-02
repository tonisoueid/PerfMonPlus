// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pmp::trace {

// A Frame flattened into named series, so the file format never depends on the shape of `Frame` (DM-06, DM-09).
// Ids are stable dotted strings ("cpu.total", "disk.0.readBps"); devices are numbered in frame order.
// `Unit` and `Availability` are stored by their enum value: new values are appended to those enums, never inserted.
struct SeriesValue {
    std::string id;
    Unit unit = Unit::Count;
    Availability avail = Availability::Pending;
    double value = 0;  // 0 unless avail == Measured
};

struct TextValue {
    std::string id, value;
};

struct TopProcess {
    std::int32_t pid = 0;
    std::uint64_t startTicks = 0;
    std::string name;  // sanitised, at most kMaxTextBytes
    Metric cpu, mem, gpu, diskRead, diskWrite;
};

struct TraceSample {
    std::chrono::system_clock::time_point wall{};
    std::int64_t monoMicros = 0;
    bool gapBefore = false;
    std::vector<SeriesValue> values;
    std::vector<TextValue> texts;
    std::vector<TopProcess> procs;
};

inline constexpr std::size_t kMaxTextBytes = 256;

// Control characters and invalid UTF-8 become '?'; longer text is cut at a character boundary and ends in "…".
std::string sanitize(std::string_view raw, std::size_t maxBytes = kMaxTextBytes);

// Keeps the union of the top `topProcesses` by CPU and by memory (CPU rank first, then memory rank), and drops
// exited processes. User names, paths, command lines and environments are never copied (NFR-14).
TraceSample flatten(const Frame& f, bool gapBefore, int topProcesses);

// Rebuilds a Frame from what was recorded. Series the sample lacks come back Unsupported; unknown series are
// ignored. The process list holds only the recorded top processes; `processes.count` is the recorded total.
Frame unflatten(const TraceSample& s, std::uint64_t generation);

}  // namespace pmp::trace
