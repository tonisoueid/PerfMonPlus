// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/proc_fs.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pmp::linux {

// One line of /proc/modules (DRV-02). Addresses are not read: they are zero for unprivileged readers anyway.
struct KernelModule {
    std::string name;
    std::uint64_t sizeBytes = 0;
    int refCount = 0;
    std::vector<std::string> usedBy;
    std::string state;  // Live | Loading | Unloading
};

std::vector<KernelModule> parseModules(std::string_view text);
std::vector<KernelModule> readKernelModules(const ProcRoot& root);  // empty when /proc/modules is unreadable

}  // namespace pmp::linux
