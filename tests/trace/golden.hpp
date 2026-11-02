// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

// The recording behind tests/data/golden-1.pmptrace. Everything here is fixed arithmetic on exact binary
// fractions, so the same bytes come out on every host. Do not "improve" it: changing it changes the golden file.

#include <perfmonplus/trace/recorder.hpp>

namespace pmp::testing {

inline Frame goldenFrame(int i) {
    const int shift = i >= 12 ? 40 : 0;  // a 40-second hole (sleep) before sample 12
    Frame f;
    f.generation = static_cast<std::uint64_t>(i + 1);
    f.mono = MonoTime{} + std::chrono::seconds(i + shift);
    f.wall = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(1'700'000'000'000 + (i + shift) * 1000LL);
    f.cpu.total = Metric::measured(10.0 + i * 2.5, Unit::Percent);
    f.cpu.kernel = Metric::measured(2.0 + i * 0.25, Unit::Percent);
    f.cpu.uptime = Metric::measured(1000.0 + i, Unit::Seconds);
    f.cpu.freqHz = Metric::measured(2.0e9 + i * 1.0e7, Unit::Hertz);
    f.cpu.governor = "performance";
    for (int c = 0; c < 4; ++c) {
        LogicalCpu l;
        l.id = c;
        l.coreClass = c < 2 ? CoreClass::Performance : CoreClass::Efficiency;
        l.util = Metric::measured(5.0 * c + i, Unit::Percent);
        l.kernel = Metric::measured(1.0 * c, Unit::Percent);
        l.freqHz = Metric::measured(1.5e9 + c * 5.0e8, Unit::Hertz);
        f.cpu.logical.push_back(l);
    }
    f.memory.total = Metric::measured(17179869184.0, Unit::Bytes);
    f.memory.inUse = Metric::measured(8589934592.0 + i * 1048576.0, Unit::Bytes);
    f.memory.available = Metric::measured(8589934592.0 - i * 1048576.0, Unit::Bytes);
    f.disks.combined.name = "combined";
    f.disks.combined.readBps = Metric::measured(1048576.0 * i, Unit::BytesPerSec);
    f.disks.combined.writeBps = Metric::measured(0.0, Unit::BytesPerSec);  // a measured zero
    f.disks.combined.activePct = Metric::measured(i % 5 * 10.0, Unit::Percent);
    f.net.combined.name = "combined";
    f.net.combined.rxBps = Metric::measured(2048.0 * i, Unit::BytesPerSec);
    f.net.combined.txBps = Metric::notMeasured(Availability::Restricted, Unit::BytesPerSec);
    GpuSnapshot g;
    g.name = "Golden GPU";
    g.util = Metric::measured(i * 3.0, Unit::Percent);
    f.gpus.push_back(g);
    f.thermals.hotspot = Metric::measured(40.0 + i * 0.5, Unit::Celsius);
    f.thermals.pressure = ThermalPressure::Nominal;
    f.energy.watts = Metric::measured(6.5 + i * 0.25, Unit::Watts);
    f.energy.source = PowerSource::AC;
    f.processes.count = Metric::measured(5, Unit::Count);
    const char* names[] = {"compiler", "browser", "editor", "indexer", "shell"};
    for (int p = 0; p < 5; ++p) {
        ProcessRecord r;
        r.key = {100 + p, 9000u + static_cast<unsigned>(p)};
        r.name = names[p];
        r.cpu = Metric::measured(50.0 - 10.0 * p + i * 0.5, Unit::Percent);
        r.memFootprint = Metric::measured((p + 1) * 104857600.0, Unit::Bytes);
        r.gpu = Metric::notMeasured(Availability::Unsupported, Unit::Percent);
        r.diskReadBps = Metric::measured(0.0, Unit::BytesPerSec);
        r.diskWriteBps = Metric::measured(1024.0 * p, Unit::BytesPerSec);
        f.processes.processes.push_back(r);
    }
    return f;
}

inline std::vector<std::byte> goldenTrace() {
    trace::HostMeta h;
    h.os = "macOS 15";
    h.kernel = "Darwin 24";
    h.cpuModel = "Apple M-class (fabricated)";
    h.logicalCpus = 4;
    h.memoryBytes = 17179869184ull;
    trace::RecorderOptions o;
    o.app = "PerfMonPlus golden-1";
    o.topProcesses = 5;
    trace::Recorder rec(h, std::chrono::milliseconds(1000), o);
    rec.start();
    for (int i = 0; i < 20; ++i) rec.append(goldenFrame(i), i == 12);
    rec.stop();
    return rec.serialise();
}

}  // namespace pmp::testing
