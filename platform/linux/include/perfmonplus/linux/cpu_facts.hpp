// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <optional>
#include <string>

namespace pmp::linux {

// Static CPU facts (CPU-06). Collected once; anything unreadable is Unsupported / empty / nullopt.
struct CpuFacts {
    std::string model;
    int sockets = 0, physicalCores = 0, logical = 0;
    Metric l1d, l1i, l2, l3;  // Bytes
    Metric baseHz, maxHz;     // Hertz
    std::string driver, governor, epp;
    std::optional<bool> virtualization, virtualMachine;
};

CpuFacts collectCpuFacts(const ProcRoot& root);

// "48K" -> 49152, "30M" -> 31457280. nullopt if unparsable.
std::optional<double> parseCacheSize(std::string_view text);

}  // namespace pmp::linux
