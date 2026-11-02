// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <cstdint>
#include <string>

namespace pmp::trace {

// What the header of a trace says about the machine it came from (DM-08). Never a user name, host name, serial
// number or address; `model` stays empty unless the user ticked "Include identifiers".
struct HostMeta {
    std::string os, kernel, model, cpuModel;
    int logicalCpus = 0;
    std::uint64_t memoryBytes = 0;
    friend bool operator==(const HostMeta&, const HostMeta&) = default;
};

}  // namespace pmp::trace
