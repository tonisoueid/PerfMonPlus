// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

// A Frame with every recorded field set to a distinct value, and a comparison that checks each of them. Written
// independently of the trace codec: if a field is added to Frame and forgotten in the codec, the round trip fails.

#include <perfmonplus/core/frame.hpp>

#include <string>

namespace pmp::testing {

class Counter {
public:
    Metric m(Unit u) { return Metric::measured(next_++ * 1.25 + 0.5, u); }
    double next() { return next_++ * 1.25 + 0.5; }

private:
    int next_ = 1;
};

inline Frame fullFrame(int topProcesses = 40) {
    Counter c;
    Frame f;
    f.generation = 77;
    f.mono = MonoTime{} + std::chrono::milliseconds(5'000'123);
    f.wall = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(1'700'000'000'000);
    auto& cpu = f.cpu;
    cpu.total = c.m(Unit::Percent);
    cpu.kernel = c.m(Unit::Percent);
    cpu.uptime = c.m(Unit::Seconds);
    cpu.freqHz = c.m(Unit::Hertz);
    cpu.throttleEvents = c.m(Unit::Count);
    cpu.driver = "intel_pstate";
    cpu.governor = "powersave";
    cpu.epp = "balance_performance";
    for (int i = 0; i < 2; ++i) {
        LogicalCpu l;
        l.id = 10 + i;
        l.coreClass = i == 0 ? CoreClass::Performance : CoreClass::Efficiency;
        l.util = c.m(Unit::Percent);
        l.kernel = c.m(Unit::Percent);
        l.freqHz = c.m(Unit::Hertz);
        l.freqMinHz = c.m(Unit::Hertz);
        l.freqMaxHz = c.m(Unit::Hertz);
        cpu.logical.push_back(l);
    }
    auto& m = f.memory;
    for (Metric* p : {&m.total, &m.inUse, &m.available, &m.committed, &m.cached, &m.buffers, &m.swapTotal, &m.swapUsed,
                      &m.swapAvailable, &m.compressed, &m.pressureSomeAvg10, &m.pageIns, &m.pageOuts}) {
        *p = c.m(Unit::Bytes);
    }
    auto disk = [&](const char* name) {
        DiskSnapshot d;
        d.name = name;
        d.model = std::string("Model ") + name;
        d.type = DiskType::SSD;
        d.readBps = c.m(Unit::BytesPerSec);
        d.writeBps = c.m(Unit::BytesPerSec);
        d.activePct = c.m(Unit::Percent);
        d.avgResponseMs = c.m(Unit::Milliseconds);
        d.totalRead = c.m(Unit::Bytes);
        d.totalWritten = c.m(Unit::Bytes);
        d.capacity = c.m(Unit::Bytes);
        return d;
    };
    f.disks.combined = disk("combined");
    f.disks.combined.type = DiskType::Mixed;
    f.disks.devices = {disk("nvme0n1"), disk("sda")};
    f.disks.devices[1].type = DiskType::HDD;
    auto net = [&](const char* name, bool isVirtual, bool up) {
        NetInterface n;
        n.name = name;
        n.isVirtual = isVirtual;
        n.up = up;
        n.rxBps = c.m(Unit::BytesPerSec);
        n.txBps = c.m(Unit::BytesPerSec);
        n.totalRx = c.m(Unit::Bytes);
        n.totalTx = c.m(Unit::Bytes);
        return n;
    };
    f.net.combined = net("combined", false, true);
    f.net.interfaces = {net("eth0", false, true), net("docker0", true, false)};
    GpuSnapshot g;
    g.index = 3;
    g.name = "Test GPU";
    g.driver = "amdgpu";
    g.util = c.m(Unit::Percent);
    g.memUsed = c.m(Unit::Bytes);
    g.memTotal = c.m(Unit::Bytes);
    g.freqHz = c.m(Unit::Hertz);
    g.temperature = c.m(Unit::Celsius);
    g.power = c.m(Unit::Watts);
    g.visibleClientsOnly = true;
    g.engines = {{"render", c.m(Unit::Percent)}, {"video", c.m(Unit::Percent)}};
    f.gpus = {g};
    f.npus = {{0, "Test NPU", c.m(Unit::Percent)}};
    auto& e = f.energy;
    e.watts = c.m(Unit::Watts);
    e.wattsSource = WattsSource::CpuPackage;
    e.source = PowerSource::Battery;
    e.powerMode = "Balanced";
    e.restrictedPath = "/sys/class/powercap/intel-rapl:0/energy_uj";
    e.domains = {{"package-0", "Package", c.m(Unit::Watts)}, {"dram", "Memory (DRAM)", c.m(Unit::Watts)},
                 {"core", "CPU cores", c.m(Unit::Watts)}};
    auto& b = e.battery;
    b.present = true;
    b.status = "Discharging";
    b.timeIsToFull = true;
    b.chargePct = c.m(Unit::Percent);
    b.timeRemaining = c.m(Unit::Seconds);
    b.healthPct = c.m(Unit::Percent);
    b.cycleCount = c.m(Unit::Count);
    b.rateW = c.m(Unit::Watts);
    b.voltageV = c.m(Unit::Volts);
    b.temperatureC = c.m(Unit::Celsius);
    b.designWh = c.m(Unit::WattHours);
    b.fullWh = c.m(Unit::WattHours);
    b.currentWh = c.m(Unit::WattHours);
    auto& t = f.thermals;
    t.hotspot = c.m(Unit::Celsius);
    t.pressure = ThermalPressure::Moderate;
    t.sensors = {{"hwmon0/temp1", "Package id 0", c.m(Unit::Celsius)}, {"hwmon1/temp1", "Composite", c.m(Unit::Celsius)}};
    t.fans = {{"hwmon2/fan1", "CPU fan", c.m(Unit::Rpm)}};
    f.processes.count = Metric::measured(topProcesses, Unit::Count);
    f.processes.threadCount = c.m(Unit::Count);
    for (int i = 0; i < topProcesses; ++i) {
        ProcessRecord r;
        r.key = {1000 + i, 5000u + static_cast<unsigned>(i)};
        r.name = "proc" + std::to_string(i);
        r.user = "alice";                       // must never reach a trace
        r.exe = "/home/alice/secret/app";       // ditto
        r.cmdline = "--token=abc";              // ditto
        r.cpu = Metric::measured((i * 37) % 100 / 2.0, Unit::Percent);
        r.memFootprint = Metric::measured(((i * 53) % 97) * 1e6, Unit::Bytes);
        r.gpu = c.m(Unit::Percent);
        r.diskReadBps = c.m(Unit::BytesPerSec);
        r.diskWriteBps = c.m(Unit::BytesPerSec);
        f.processes.processes.push_back(r);
    }
    return f;
}

inline bool sameMetric(const Metric& a, const Metric& b) {
    return a.availability == b.availability && a.unit == b.unit && a.value == b.value;
}

// Everything the trace records (not generation, boot, provider health, or per-process fields beyond the top rows).
inline bool sameRecorded(const Frame& a, const Frame& b, std::string* why = nullptr) {
    auto fail = [&](const char* what) {
        if (why) *why = what;
        return false;
    };
#define PMP_M(x) if (!sameMetric(a.x, b.x)) return fail(#x)
    PMP_M(cpu.total); PMP_M(cpu.kernel); PMP_M(cpu.uptime); PMP_M(cpu.freqHz); PMP_M(cpu.throttleEvents);
    if (a.cpu.driver != b.cpu.driver || a.cpu.governor != b.cpu.governor || a.cpu.epp != b.cpu.epp) return fail("cpu text");
    if (a.cpu.logical.size() != b.cpu.logical.size()) return fail("logical count");
    for (std::size_t i = 0; i < a.cpu.logical.size(); ++i) {
        const auto &x = a.cpu.logical[i], &y = b.cpu.logical[i];
        if (x.id != y.id || x.coreClass != y.coreClass) return fail("logical id/class");
        for (auto [p, q] : {std::pair{&x.util, &y.util}, {&x.kernel, &y.kernel}, {&x.freqHz, &y.freqHz},
                            {&x.freqMinHz, &y.freqMinHz}, {&x.freqMaxHz, &y.freqMaxHz}}) {
            if (!sameMetric(*p, *q)) return fail("logical metric");
        }
    }
    PMP_M(memory.total); PMP_M(memory.inUse); PMP_M(memory.available); PMP_M(memory.committed); PMP_M(memory.cached);
    PMP_M(memory.buffers); PMP_M(memory.swapTotal); PMP_M(memory.swapUsed); PMP_M(memory.swapAvailable);
    PMP_M(memory.compressed); PMP_M(memory.pressureSomeAvg10); PMP_M(memory.pageIns); PMP_M(memory.pageOuts);
    auto sameDisk = [&](const DiskSnapshot& x, const DiskSnapshot& y) {
        return x.name == y.name && x.model == y.model && x.type == y.type && sameMetric(x.readBps, y.readBps) &&
               sameMetric(x.writeBps, y.writeBps) && sameMetric(x.activePct, y.activePct) &&
               sameMetric(x.avgResponseMs, y.avgResponseMs) && sameMetric(x.totalRead, y.totalRead) &&
               sameMetric(x.totalWritten, y.totalWritten) && sameMetric(x.capacity, y.capacity);
    };
    if (!sameDisk(a.disks.combined, b.disks.combined) || a.disks.devices.size() != b.disks.devices.size()) return fail("disks");
    for (std::size_t i = 0; i < a.disks.devices.size(); ++i) {
        if (!sameDisk(a.disks.devices[i], b.disks.devices[i])) return fail("disk device");
    }
    auto sameNet = [&](const NetInterface& x, const NetInterface& y) {
        return x.name == y.name && x.isVirtual == y.isVirtual && x.up == y.up && sameMetric(x.rxBps, y.rxBps) &&
               sameMetric(x.txBps, y.txBps) && sameMetric(x.totalRx, y.totalRx) && sameMetric(x.totalTx, y.totalTx);
    };
    if (!sameNet(a.net.combined, b.net.combined) || a.net.interfaces.size() != b.net.interfaces.size()) return fail("net");
    for (std::size_t i = 0; i < a.net.interfaces.size(); ++i) {
        if (!sameNet(a.net.interfaces[i], b.net.interfaces[i])) return fail("net interface");
    }
    if (a.gpus.size() != b.gpus.size()) return fail("gpu count");
    for (std::size_t i = 0; i < a.gpus.size(); ++i) {
        const auto &x = a.gpus[i], &y = b.gpus[i];
        if (x.index != y.index || x.name != y.name || x.driver != y.driver || x.visibleClientsOnly != y.visibleClientsOnly)
            return fail("gpu text");
        for (auto [p, q] : {std::pair{&x.util, &y.util}, {&x.memUsed, &y.memUsed}, {&x.memTotal, &y.memTotal},
                            {&x.freqHz, &y.freqHz}, {&x.temperature, &y.temperature}, {&x.power, &y.power}}) {
            if (!sameMetric(*p, *q)) return fail("gpu metric");
        }
        if (x.engines.size() != y.engines.size()) return fail("gpu engines");
        for (std::size_t j = 0; j < x.engines.size(); ++j) {
            if (x.engines[j].name != y.engines[j].name || !sameMetric(x.engines[j].util, y.engines[j].util)) return fail("engine");
        }
    }
    if (a.npus.size() != b.npus.size()) return fail("npu count");
    for (std::size_t i = 0; i < a.npus.size(); ++i) {
        if (a.npus[i].name != b.npus[i].name || !sameMetric(a.npus[i].activePct, b.npus[i].activePct)) return fail("npu");
    }
    PMP_M(energy.watts);
    if (a.energy.wattsSource != b.energy.wattsSource || a.energy.source != b.energy.source ||
        a.energy.powerMode != b.energy.powerMode || a.energy.restrictedPath != b.energy.restrictedPath) return fail("energy text");
    if (a.energy.domains.size() != b.energy.domains.size()) return fail("domain count");
    for (std::size_t i = 0; i < a.energy.domains.size(); ++i) {
        const auto &x = a.energy.domains[i], &y = b.energy.domains[i];
        if (x.id != y.id || x.label != y.label || !sameMetric(x.watts, y.watts)) return fail("domain");
    }
    const auto &x = a.energy.battery, &y = b.energy.battery;
    if (x.present != y.present || x.status != y.status || x.timeIsToFull != y.timeIsToFull) return fail("battery text");
    for (auto [p, q] : {std::pair{&x.chargePct, &y.chargePct}, {&x.timeRemaining, &y.timeRemaining}, {&x.healthPct, &y.healthPct},
                        {&x.cycleCount, &y.cycleCount}, {&x.rateW, &y.rateW}, {&x.voltageV, &y.voltageV},
                        {&x.temperatureC, &y.temperatureC}, {&x.designWh, &y.designWh}, {&x.fullWh, &y.fullWh},
                        {&x.currentWh, &y.currentWh}}) {
        if (!sameMetric(*p, *q)) return fail("battery metric");
    }
    PMP_M(thermals.hotspot);
    if (a.thermals.pressure != b.thermals.pressure) return fail("pressure");
    if (a.thermals.sensors.size() != b.thermals.sensors.size() || a.thermals.fans.size() != b.thermals.fans.size()) return fail("sensor count");
    for (std::size_t i = 0; i < a.thermals.sensors.size(); ++i) {
        const auto &s = a.thermals.sensors[i], &t = b.thermals.sensors[i];
        if (s.id != t.id || s.label != t.label || !sameMetric(s.celsius, t.celsius)) return fail("sensor");
    }
    for (std::size_t i = 0; i < a.thermals.fans.size(); ++i) {
        const auto &s = a.thermals.fans[i], &t = b.thermals.fans[i];
        if (s.id != t.id || s.label != t.label || !sameMetric(s.rpm, t.rpm)) return fail("fan");
    }
    PMP_M(processes.count); PMP_M(processes.threadCount);
#undef PMP_M
    return true;
}

}  // namespace pmp::testing
