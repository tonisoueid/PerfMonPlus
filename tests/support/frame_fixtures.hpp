// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

namespace pmp::testing {

constexpr double KiB = 1024.0;
constexpr double GiB = KiB * KiB * KiB;

inline Metric pct(double v) { return Metric::measured(v, Unit::Percent); }
inline Metric bytes(double v) { return Metric::measured(v, Unit::Bytes); }
inline Metric bps(double v) { return Metric::measured(v, Unit::BytesPerSec); }

// A realistic populated frame: 15.8% CPU, 66.29 GB of 128 GB, quiet disk, small network.
inline Frame populatedFrame(int logicalCpus = 8, int processes = 15) {
    Frame f;
    f.generation = 122;
    f.mono = MonoTime{} + std::chrono::seconds(1000);
    f.cpu.total = pct(15.8);
    f.cpu.kernel = pct(3.1);
    f.cpu.uptime = Metric::measured(485479, Unit::Seconds);
    f.cpu.freqHz = Metric::notMeasured(Availability::Unsupported, Unit::Hertz);
    f.thermals.hotspot = Metric::notMeasured(Availability::Unsupported, Unit::Celsius);
    f.energy.watts = Metric::notMeasured(Availability::Unsupported, Unit::Watts);
    f.cpu.logical.resize(static_cast<std::size_t>(logicalCpus));
    for (int i = 0; i < logicalCpus; ++i) {
        f.cpu.logical[static_cast<std::size_t>(i)].id = i;
        f.cpu.logical[static_cast<std::size_t>(i)].util = pct(10.0 + i);
    }
    f.memory.total = bytes(128 * GiB);
    f.memory.inUse = bytes(66.29 * GiB);
    f.memory.available = bytes(61.71 * GiB);
    f.memory.cached = bytes(33.02 * GiB);
    f.memory.swapUsed = bytes(0);
    f.memory.swapTotal = bytes(0);
    f.memory.swapAvailable = bytes(0);
    f.disks.combined.name = "combined";
    f.disks.combined.activePct = pct(0.0);
    f.disks.combined.readBps = bps(0);
    f.disks.combined.writeBps = bps(32 * KiB);
    f.net.combined.name = "combined";
    f.net.combined.rxBps = bps(14 * KiB);
    f.net.combined.txBps = bps(8 * KiB);
    f.processes.count = Metric::measured(processes, Unit::Count);
    f.processes.threadCount = Metric::measured(processes * 4, Unit::Count);
    for (int i = 1; i <= processes; ++i) {
        ProcessRecord r;
        r.key = {i, static_cast<std::uint64_t>(1000 + i)};
        r.name = "proc" + std::to_string(i);
        r.cpu = pct(i);
        r.memFootprint = bytes(i * 10 * KiB * KiB);
        f.processes.processes.push_back(r);
    }
    f.providerHealth.resize(5);
    return f;
}

// Adds a realistic laptop's GPU, battery/energy and thermal data to a frame.
inline Frame& withHardware(Frame& f) {
    GpuSnapshot g;
    g.index = 0;
    g.name = "Intel Iris Xe Graphics";
    g.driver = "i915";
    g.pciSlot = "0000:00:02.0";
    g.util = pct(18);
    g.memUsed = bytes(394480 * KiB);
    g.memTotal = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
    g.freqHz = Metric::measured(1.3e9, Unit::Hertz);
    g.temperature = Metric::notMeasured(Availability::Unsupported, Unit::Celsius);
    g.power = Metric::notMeasured(Availability::Unsupported, Unit::Watts);
    g.engines = {{"render", pct(18)}, {"copy", pct(0)}, {"video", pct(0)}};
    f.gpus = {g};
    f.cpu.freqHz = Metric::measured(1.5e9, Unit::Hertz);
    f.energy.watts = Metric::measured(9.753, Unit::Watts);
    f.energy.wattsSource = WattsSource::BatteryDischarge;
    f.energy.source = PowerSource::Battery;
    f.energy.powerMode = "Balance power";
    f.energy.battery.present = true;
    f.energy.battery.status = "Discharging";
    f.energy.battery.chargePct = pct(89);
    f.energy.battery.healthPct = pct(81);
    f.energy.battery.cycleCount = Metric::measured(58, Unit::Count);
    f.energy.battery.rateW = Metric::measured(9.753, Unit::Watts);
    f.energy.battery.timeRemaining = Metric::measured(21339, Unit::Seconds);
    f.thermals.hotspot = Metric::measured(49, Unit::Celsius);
    f.thermals.pressure = ThermalPressure::Nominal;
    f.thermals.sensors = {{"hwmon:coretemp:temp1", "coretemp Package id 0", Metric::measured(49, Unit::Celsius)},
                          {"hwmon:coretemp:temp2", "coretemp Core 0", Metric::measured(47, Unit::Celsius)},
                          {"hwmon:nvme:temp1", "nvme Composite", Metric::measured(38, Unit::Celsius)}};
    return f;
}

}  // namespace pmp::testing
