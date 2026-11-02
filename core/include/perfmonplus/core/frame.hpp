// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>

#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pmp {

enum class CoreClass { Performance, Efficiency, Unknown };
enum class DiskType { SSD, HDD, Mixed, Unknown };
enum class HealthState { Healthy, Degraded, Failed, Disabled };

struct LogicalCpu {
    int id = 0;
    CoreClass coreClass = CoreClass::Unknown;
    Metric util, kernel;
    Metric freqHz, freqMinHz, freqMaxHz;  // cpufreq; Unsupported when the core has none
};

struct CpuSnapshot {
    Metric total, kernel, uptime, freqHz;
    Metric throttleEvents;                // cumulative thermal-throttle events since boot
    std::string driver, governor, epp;    // "" when not reported
    std::vector<LogicalCpu> logical;
};

struct MemorySnapshot {
    Metric total, inUse, available, committed, cached, buffers, swapTotal, swapUsed, swapAvailable,
        compressed, pressureSomeAvg10, pageIns, pageOuts;
};

struct DiskSnapshot {
    std::string name, model;
    DiskType type = DiskType::Unknown;
    Metric readBps, writeBps, activePct, avgResponseMs, totalRead, totalWritten, capacity;
};

struct DisksSnapshot {
    std::vector<DiskSnapshot> devices;
    DiskSnapshot combined;
};

struct NetInterface {
    std::string name;
    bool isVirtual = false;
    bool up = false;
    Metric rxBps, txBps, totalRx, totalTx;
};

struct NetSnapshot {
    std::vector<NetInterface> interfaces;
    NetInterface combined;
};

// Identity = PID + OS start time, so a recycled PID is a different process (INT-05).
struct ProcessKey {
    std::int32_t pid = 0;
    std::uint64_t startTicks = 0;
    friend auto operator<=>(const ProcessKey&, const ProcessKey&) = default;
};

enum class ProcessKind { App, Background, System };

struct ProcessRecord {
    ProcessKey key;
    std::optional<ProcessKey> parent;
    std::string name, user;
    char state = '?';
    Metric cpu, gpu, memFootprint, memPrivate, threads, diskReadBps, diskWriteBps;
    std::uint32_t uid = 0;          // 0xFFFFFFFF (pmp::linux::kUnknownUid) when it could not be read
    bool kernelThread = false;      // PF_KTHREAD in the stat flags: true inside PID namespaces too, unlike "parent is pid 2"
    std::string exe;                // target of /proc/[pid]/exe; "" when there is none or it is unknown
    bool exeRestricted = false;     // the link exists but this user may not read it
    ProcessKind kind = ProcessKind::Background;
    std::string appScope;           // the app.slice unit the process belongs to; "" when none
    std::optional<std::chrono::system_clock::time_point> started;
    Metric cpuTime;                 // total CPU seconds (utime + stime)
    Metric priority;                // nice value
    Metric pageFaults;              // minor + major faults since start
    std::string cmdline;            // only filled while collection is on (privacy); "" otherwise
    std::optional<MonoTime> exitedAt;  // engaged => tombstone (PRC-09)
};

struct ProcessTable {
    std::vector<ProcessRecord> processes;
    Metric count, threadCount;
};

struct GpuEngine {
    std::string name;
    Metric util;
};

struct GpuSnapshot {
    int index = 0;
    std::string name, driver, pciSlot;
    Metric util, memUsed, memTotal, freqHz, temperature, power;
    std::vector<GpuEngine> engines;
    bool visibleClientsOnly = false;  // true when other users' GPU clients could not be inspected
};

struct NpuSnapshot {
    int index = 0;
    std::string name;
    Metric activePct;
};

enum class PowerSource { AC, Battery, Unknown };
enum class WattsSource { CpuPackage, BatteryDischarge, None };

struct BatteryInfo {
    bool present = false;
    std::string status;  // Charging | Discharging | Full | Not charging | Unknown
    Metric chargePct, timeRemaining, healthPct, cycleCount, rateW;
    Metric voltageV, temperatureC, designWh, fullWh, currentWh;
    bool timeIsToFull = false;
};

struct PowerDomain {
    std::string id, label;
    Metric watts;
};

struct EnergySnapshot {
    Metric watts;
    std::vector<PowerDomain> domains;  // RAPL domains: package, cores, uncore, dram, platform
    WattsSource wattsSource = WattsSource::None;
    PowerSource source = PowerSource::Unknown;
    std::string powerMode;
    BatteryInfo battery;
    std::string restrictedPath;  // set when a power counter exists but only root can read it (ENE-08)
};

enum class ThermalPressure { Nominal, Moderate, Heavy, Critical, Unknown };

struct SensorReading {
    std::string id, label;
    Metric celsius;
};

struct FanReading {
    std::string id, label;
    Metric rpm;
};

struct ThermalSnapshot {
    Metric hotspot;
    ThermalPressure pressure = ThermalPressure::Unknown;
    std::vector<SensorReading> sensors;
    std::vector<FanReading> fans;
};

// Health says whether the provider's code ran, not whether its source was readable: a missing or denied source is
// reported by each metric's Availability (Unsupported / Restricted), never as a provider fault.
struct ProviderHealth {
    std::string id;
    HealthState state = HealthState::Healthy;
    std::string lastError;
    int consecutiveFailures = 0;
    double avgSampleCostMicros = 0;
};

struct Frame {
    std::uint64_t generation = 0;
    MonoTime mono{};
    std::chrono::nanoseconds boot{};
    std::chrono::system_clock::time_point wall{};
    CpuSnapshot cpu;
    MemorySnapshot memory;
    DisksSnapshot disks;
    NetSnapshot net;
    ProcessTable processes;
    std::vector<GpuSnapshot> gpus;
    std::vector<NpuSnapshot> npus;
    EnergySnapshot energy;
    ThermalSnapshot thermals;
    std::vector<ProviderHealth> providerHealth;
};

}  // namespace pmp
