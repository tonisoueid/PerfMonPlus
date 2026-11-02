// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <string>
#include <utility>
#include <vector>

namespace pmp::vm {

// One core's frequency line (PWR-02). `fraction` is current / maximum, 0 when either is unknown.
struct FreqRow {
    std::string label;
    CoreClass coreClass = CoreClass::Unknown;
    std::string cur, min, max;
    double fraction = 0;
    bool available = false;
};

// One power domain (PWR-01).
struct PowerRow {
    std::string id, label, value;
    bool restricted = false;
};

struct BatteryCard {
    std::vector<std::pair<std::string, std::string>> rows;  // caption, value (PWR-03)
};

struct PowerFreqModel {
    std::vector<FreqRow> cores;
    std::string governor, epp, driver, throttle;
    std::vector<PowerRow> domains;
    std::string restrictedHelp;  // empty unless a power counter is root-only
    BatteryCard battery;
};

PowerFreqModel buildPowerFreq(const Frame& f);

}  // namespace pmp::vm
