// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/trace/meta.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pmp::trace {

inline constexpr std::uint32_t kFormatVersion = 1;

// The self-describing header of a trace (DM-06, DM-08) plus what the player learned from the body.
struct TraceInfo {
    std::uint32_t format = kFormatVersion;
    std::string codec = "deflate-shuffle8";
    std::string app;  // "PerfMonPlus 0.1.0"
    HostMeta host;
    std::int64_t startWallMs = 0;
    int intervalMs = 1000;
    int topProcesses = 20;
    // Filled by the player:
    std::size_t sampleCount = 0;
    std::chrono::milliseconds duration{0};
};

}  // namespace pmp::trace
