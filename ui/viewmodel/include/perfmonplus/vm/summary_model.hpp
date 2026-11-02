// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pmp::vm {

struct VfdColumn {
    std::string label, value;
    double fraction = 0;  // 0..1, only meaningful when measured
    bool measured = false;
};
// CPU, Clock, Temp, GPU. A column whose hardware reports nothing reads "Unavailable".
std::array<VfdColumn, 4> vfdStack(const Frame& f, double clockFullScaleHz = 5e9);

struct TopProcessRow {
    ProcessKey key;  // identity, so a click can find the very same process again
    std::int32_t pid = 0;
    std::string name, cpu, gpu, memory;
};
// Live processes only, CPU descending, unmeasured CPU last. GPU is the process's own GPU use ("—" until measured).
std::vector<TopProcessRow> topProcesses(const Frame& f, std::size_t n);

std::string memoryHeader(const Frame& f);  // "{in use} / {total}" or "Unavailable"

struct TileText {
    std::string title, value, subtitle;
    bool available = false;
};
TileText diskTile(const Frame& f);     // value: active time; subtitle "R: {read}  W: {write}"
TileText networkTile(const Frame& f);  // value: rx + tx;     subtitle "R: {rx}  S: {tx}"

}  // namespace pmp::vm
