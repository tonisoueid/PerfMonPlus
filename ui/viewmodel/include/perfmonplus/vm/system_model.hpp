// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/system_info.hpp>
#include <perfmonplus/vm/performance_model.hpp>

#include <string>
#include <vector>

namespace pmp::vm {

struct InfoRow {
    std::string key, value;
    bool identifier = false;  // serial numbers and MAC addresses: hidden unless the user opts in (NFR-14)
};
struct InfoSection {
    std::string title;
    std::vector<InfoRow> rows;
};

// Overview, Processor, Memory, Graphics, Network adapters. The GUI appends Displays and Sensors.
std::vector<InfoSection> systemInfoSections(const linux::SystemInfo& s, bool includeIdentifiers);

// L1 cache .. Power preference (CPU-06). Unknown facts read "Unavailable".
std::vector<StatItem> cpuFactItems(const linux::CpuFacts& c);

// "[Title]\nKey: Value\n\n" per section.
std::string exportText(const std::vector<InfoSection>& sections);

}  // namespace pmp::vm
