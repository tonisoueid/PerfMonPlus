// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/vm/performance_model.hpp>
#include <perfmonplus/vm/summary_model.hpp>

#include <string>
#include <vector>

namespace pmp::vm {

TileText gpuTile(const GpuSnapshot& g);   // "GPU n" | utilisation | name
TileText npuTile(const NpuSnapshot& n);   // "NPU n" | active time | name
TileText energyTile(const Frame& f);      // "Energy" | watts (or Restricted) | "Thermals nominal · Battery 89%"
TileText thermalsTile(const Frame& f);    // "Thermals" | hotspot | "Nominal thermal pressure"

std::string pressureText(ThermalPressure p);              // Nominal | Moderate | Heavy | Critical | Unavailable
std::string powerSourceText(const EnergySnapshot& e);     // "AC Power" | "Battery 89%" | "Unavailable"

struct Chip {
    std::string caption, value;
};
std::vector<Chip> energyChips(const Frame& f);   // Thermal state, Power mode, Power source (ENE-03)
std::vector<Chip> thermalChips(const Frame& f);  // Thermal pressure, Hotspot (THM-03)

// Charge, Time remaining | Time to full, Health, Cycle count, Rate (ENE-04); empty without a battery.
std::vector<StatItem> batteryStats(const EnergySnapshot& e);

struct EnergyRow {
    ProcessKey key;
    std::string name;
    double score = 0;   // 0..100, the top process = 100
    std::string value;  // "15 W" when package power is known, else the score
};
// Live processes with a measured CPU share, highest first (ENE-05/06).
std::vector<EnergyRow> energyDemand(const Frame& f, std::size_t n = 8);
std::string energyFootnote(const Frame& f);                // how the demand was estimated (ENE-07)
std::string restrictedPowerHelp(const EnergySnapshot& e);  // ENE-08; empty unless a power counter is root-only

}  // namespace pmp::vm
