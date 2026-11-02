// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/frame_codec.hpp>

#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace pmp::trace {
namespace {

std::string idx(std::string_view a, std::size_t i, std::string_view b = {}) {
    std::string s(a);
    s += '.';
    s += std::to_string(i);
    if (!b.empty()) {
        s += '.';
        s += b;
    }
    return s;
}

// One traversal of a Frame serves both directions. `Out` copies the Frame's values into a sample; `In` copies a
// sample's values into a Frame. A new Frame field is added by one line in visit(), and the round-trip test over a
// frame with every field set fails if that line is forgotten.
struct Out {
    TraceSample& s;

    void metric(std::string id, const Metric& m) {
        s.values.push_back({std::move(id), m.unit, m.availability, m.isMeasured() && m.value ? *m.value : 0.0});
    }
    void text(std::string id, const std::string& v) { s.texts.push_back({std::move(id), sanitize(v)}); }
    void integer(std::string id, const long long& v) { s.texts.push_back({std::move(id), std::to_string(v)}); }
    template <class E>
    void enumv(std::string id, const E& e) { s.texts.push_back({std::move(id), std::to_string(static_cast<int>(e))}); }
    template <class V>
    std::size_t count(const std::string&, std::string_view, V& vec) { return vec.size(); }
};

struct In {
    std::unordered_map<std::string_view, const SeriesValue*> values;
    std::unordered_map<std::string_view, const std::string*> texts;

    explicit In(const TraceSample& s) {
        for (const auto& v : s.values) values.emplace(v.id, &v);
        for (const auto& t : s.texts) texts.emplace(t.id, &t.value);
    }
    void metric(const std::string& id, Metric& m) const {
        const auto it = values.find(id);
        if (it == values.end()) {
            m = Metric::notMeasured(Availability::Unsupported, Unit::Count);
            return;
        }
        const auto& v = *it->second;
        const auto a = static_cast<int>(v.avail);
        const auto avail = a >= 0 && a <= static_cast<int>(Availability::Disabled) ? v.avail : Availability::Failed;
        m = avail == Availability::Measured ? Metric::measured(v.value, v.unit) : Metric::notMeasured(avail, v.unit);
    }
    void text(const std::string& id, std::string& v) const {
        const auto it = texts.find(id);
        v = it == texts.end() ? std::string() : *it->second;
    }
    void integer(const std::string& id, long long& v) const {
        const auto it = texts.find(id);
        v = 0;
        if (it != texts.end()) std::from_chars(it->second->data(), it->second->data() + it->second->size(), v);
    }
    template <class E>
    void enumv(const std::string& id, E& e) const {
        long long v = 0;
        integer(id, v);
        e = static_cast<E>(v);
    }
    template <class V>
    std::size_t count(const std::string& base, std::string_view marker, V& vec) const {
        std::size_t n = 0;
        // A frame never has more than a few thousand of anything; the cap keeps a hostile file from looping forever.
        while (n < 4096) {
            const auto id = idx(base, n, marker);
            if (!values.contains(id) && !texts.contains(id)) break;
            ++n;
        }
        vec.resize(n);
        return n;
    }
};

// `long long` shims so one visit() handles int and bool members in both directions.
template <class Io, class T>
void integerField(Io& io, const std::string& id, T& field) {
    if constexpr (std::is_const_v<T>) {
        long long v = static_cast<long long>(field);
        io.integer(id, v);
    } else {
        long long v = static_cast<long long>(field);
        io.integer(id, v);
        field = static_cast<T>(v);
    }
}

template <class Io, class D>
void visitDisk(Io& io, const std::string& p, D& d) {
    io.text(p + ".name", d.name);
    io.text(p + ".model", d.model);
    io.enumv(p + ".type", d.type);
    io.metric(p + ".readBps", d.readBps);
    io.metric(p + ".writeBps", d.writeBps);
    io.metric(p + ".activePct", d.activePct);
    io.metric(p + ".avgResponseMs", d.avgResponseMs);
    io.metric(p + ".totalRead", d.totalRead);
    io.metric(p + ".totalWritten", d.totalWritten);
    io.metric(p + ".capacity", d.capacity);
}

template <class Io, class N>
void visitNet(Io& io, const std::string& p, N& n) {
    io.text(p + ".name", n.name);
    integerField(io, p + ".virtual", n.isVirtual);
    integerField(io, p + ".up", n.up);
    io.metric(p + ".rxBps", n.rxBps);
    io.metric(p + ".txBps", n.txBps);
    io.metric(p + ".totalRx", n.totalRx);
    io.metric(p + ".totalTx", n.totalTx);
}

template <class Io, class F>
void visit(Io& io, F& f) {
    // ---- CPU ----
    io.metric("cpu.total", f.cpu.total);
    io.metric("cpu.kernel", f.cpu.kernel);
    io.metric("cpu.uptime", f.cpu.uptime);
    io.metric("cpu.freq", f.cpu.freqHz);
    io.metric("cpu.throttle", f.cpu.throttleEvents);
    io.text("cpu.driver", f.cpu.driver);
    io.text("cpu.governor", f.cpu.governor);
    io.text("cpu.epp", f.cpu.epp);
    const auto nCpu = io.count("cpu.logical", "util", f.cpu.logical);
    for (std::size_t i = 0; i < nCpu; ++i) {
        auto& l = f.cpu.logical[i];
        const auto p = idx("cpu.logical", i);
        integerField(io, p + ".id", l.id);
        io.enumv(p + ".class", l.coreClass);
        io.metric(p + ".util", l.util);
        io.metric(p + ".kernel", l.kernel);
        io.metric(p + ".freq", l.freqHz);
        io.metric(p + ".fmin", l.freqMinHz);
        io.metric(p + ".fmax", l.freqMaxHz);
    }

    // ---- memory ----
    auto& m = f.memory;
    io.metric("mem.total", m.total);
    io.metric("mem.inUse", m.inUse);
    io.metric("mem.available", m.available);
    io.metric("mem.committed", m.committed);
    io.metric("mem.cached", m.cached);
    io.metric("mem.buffers", m.buffers);
    io.metric("mem.swapTotal", m.swapTotal);
    io.metric("mem.swapUsed", m.swapUsed);
    io.metric("mem.swapAvailable", m.swapAvailable);
    io.metric("mem.compressed", m.compressed);
    io.metric("mem.pressureSomeAvg10", m.pressureSomeAvg10);
    io.metric("mem.pageIns", m.pageIns);
    io.metric("mem.pageOuts", m.pageOuts);

    // ---- disks and network ----
    visitDisk(io, "disk.combined", f.disks.combined);
    const auto nDisk = io.count("disk", "name", f.disks.devices);
    for (std::size_t i = 0; i < nDisk; ++i) visitDisk(io, idx("disk", i), f.disks.devices[i]);
    visitNet(io, "net.combined", f.net.combined);
    const auto nNet = io.count("net", "name", f.net.interfaces);
    for (std::size_t i = 0; i < nNet; ++i) visitNet(io, idx("net", i), f.net.interfaces[i]);

    // ---- GPU and NPU ----
    const auto nGpu = io.count("gpu", "name", f.gpus);
    for (std::size_t i = 0; i < nGpu; ++i) {
        auto& g = f.gpus[i];
        const auto p = idx("gpu", i);
        integerField(io, p + ".index", g.index);
        io.text(p + ".name", g.name);
        io.text(p + ".driver", g.driver);
        integerField(io, p + ".visibleClientsOnly", g.visibleClientsOnly);
        io.metric(p + ".util", g.util);
        io.metric(p + ".memUsed", g.memUsed);
        io.metric(p + ".memTotal", g.memTotal);
        io.metric(p + ".freq", g.freqHz);
        io.metric(p + ".temp", g.temperature);
        io.metric(p + ".power", g.power);
        const auto nEng = io.count(p + ".engine", "name", g.engines);
        for (std::size_t j = 0; j < nEng; ++j) {
            const auto ep = idx(p + ".engine", j);
            io.text(ep + ".name", g.engines[j].name);
            io.metric(ep + ".util", g.engines[j].util);
        }
    }
    const auto nNpu = io.count("npu", "name", f.npus);
    for (std::size_t i = 0; i < nNpu; ++i) {
        const auto p = idx("npu", i);
        io.text(p + ".name", f.npus[i].name);
        io.metric(p + ".active", f.npus[i].activePct);
    }

    // ---- energy ----
    auto& e = f.energy;
    io.metric("energy.watts", e.watts);
    io.enumv("energy.wattsSource", e.wattsSource);
    io.enumv("energy.source", e.source);
    io.text("energy.powerMode", e.powerMode);
    io.text("energy.restrictedPath", e.restrictedPath);
    const auto nDom = io.count("energy.domain", "id", e.domains);
    for (std::size_t i = 0; i < nDom; ++i) {
        const auto p = idx("energy.domain", i);
        io.text(p + ".id", e.domains[i].id);
        io.text(p + ".label", e.domains[i].label);
        io.metric(p + ".watts", e.domains[i].watts);
    }
    auto& b = e.battery;
    integerField(io, "energy.battery.present", b.present);
    io.text("energy.battery.status", b.status);
    integerField(io, "energy.battery.timeIsToFull", b.timeIsToFull);
    io.metric("energy.battery.chargePct", b.chargePct);
    io.metric("energy.battery.timeRemaining", b.timeRemaining);
    io.metric("energy.battery.healthPct", b.healthPct);
    io.metric("energy.battery.cycleCount", b.cycleCount);
    io.metric("energy.battery.rateW", b.rateW);
    io.metric("energy.battery.voltageV", b.voltageV);
    io.metric("energy.battery.temperatureC", b.temperatureC);
    io.metric("energy.battery.designWh", b.designWh);
    io.metric("energy.battery.fullWh", b.fullWh);
    io.metric("energy.battery.currentWh", b.currentWh);

    // ---- thermals ----
    auto& t = f.thermals;
    io.metric("thermal.hotspot", t.hotspot);
    io.enumv("thermal.pressure", t.pressure);
    const auto nSens = io.count("thermal.sensor", "id", t.sensors);
    for (std::size_t i = 0; i < nSens; ++i) {
        const auto p = idx("thermal.sensor", i);
        io.text(p + ".id", t.sensors[i].id);
        io.text(p + ".label", t.sensors[i].label);
        io.metric(p + ".celsius", t.sensors[i].celsius);
    }
    const auto nFan = io.count("thermal.fan", "id", t.fans);
    for (std::size_t i = 0; i < nFan; ++i) {
        const auto p = idx("thermal.fan", i);
        io.text(p + ".id", t.fans[i].id);
        io.text(p + ".label", t.fans[i].label);
        io.metric(p + ".rpm", t.fans[i].rpm);
    }

    // ---- process totals (the rows themselves are handled by flatten/unflatten) ----
    io.metric("proc.count", f.processes.count);
    io.metric("proc.threads", f.processes.threadCount);
}

bool betterCpu(const ProcessRecord* a, const ProcessRecord* b) {
    const bool am = a->cpu.isMeasured(), bm = b->cpu.isMeasured();
    if (am != bm) return am;
    if (am && *a->cpu.value != *b->cpu.value) return *a->cpu.value > *b->cpu.value;
    return a->key < b->key;
}

bool betterMem(const ProcessRecord* a, const ProcessRecord* b) {
    const bool am = a->memFootprint.isMeasured(), bm = b->memFootprint.isMeasured();
    if (am != bm) return am;
    if (am && *a->memFootprint.value != *b->memFootprint.value) return *a->memFootprint.value > *b->memFootprint.value;
    return a->key < b->key;
}

}  // namespace

std::string sanitize(std::string_view raw, std::size_t maxBytes) {
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        const auto c = static_cast<unsigned char>(raw[i]);
        std::size_t len = 1;
        bool valid = true;
        if (c < 0x20 || c == 0x7F) {
            valid = false;
        } else if (c < 0x80) {
        } else if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
        } else {
            valid = false;
        }
        if (valid && len > 1) {
            if (i + len > raw.size()) {
                valid = false;
            } else {
                for (std::size_t k = 1; k < len; ++k) {
                    if ((static_cast<unsigned char>(raw[i + k]) & 0xC0) != 0x80) valid = false;
                }
            }
        }
        if (valid) {
            out.append(raw.substr(i, len));
            i += len;
        } else {
            out.push_back('?');
            ++i;
        }
    }
    if (out.size() <= maxBytes) return out;
    std::size_t keep = maxBytes > 3 ? maxBytes - 3 : 0;
    while (keep > 0 && (static_cast<unsigned char>(out[keep]) & 0xC0) == 0x80) --keep;  // back off to a character start
    out.resize(keep);
    return out + "\xE2\x80\xA6";
}

TraceSample flatten(const Frame& f, bool gapBefore, int topProcesses) {
    TraceSample s;
    s.wall = f.wall;
    s.monoMicros = std::chrono::duration_cast<std::chrono::microseconds>(f.mono.time_since_epoch()).count();
    s.gapBefore = gapBefore;
    Out out{s};
    visit(out, f);

    std::vector<const ProcessRecord*> live;
    for (const auto& p : f.processes.processes) {
        if (!p.exitedAt) live.push_back(&p);
    }
    const auto n = static_cast<std::size_t>(std::max(0, topProcesses));
    std::vector<const ProcessRecord*> byCpu = live, byMem = live, picked;
    const auto cpuEnd = byCpu.begin() + static_cast<long>(std::min(n, byCpu.size()));
    std::partial_sort(byCpu.begin(), cpuEnd, byCpu.end(), betterCpu);
    const auto memEnd = byMem.begin() + static_cast<long>(std::min(n, byMem.size()));
    std::partial_sort(byMem.begin(), memEnd, byMem.end(), betterMem);
    for (auto it = byCpu.begin(); it != cpuEnd; ++it) picked.push_back(*it);
    for (auto it = byMem.begin(); it != memEnd; ++it) {
        if (std::find(picked.begin(), picked.end(), *it) == picked.end()) picked.push_back(*it);
    }
    for (const auto* p : picked) {
        TopProcess t;
        t.pid = p->key.pid;
        t.startTicks = p->key.startTicks;
        t.name = sanitize(p->name);
        t.cpu = p->cpu;
        t.mem = p->memFootprint;
        t.gpu = p->gpu;
        t.diskRead = p->diskReadBps;
        t.diskWrite = p->diskWriteBps;
        s.procs.push_back(std::move(t));
    }
    return s;
}

Frame unflatten(const TraceSample& s, std::uint64_t generation) {
    Frame f;
    f.generation = generation;
    f.wall = s.wall;
    f.mono = MonoTime{} + std::chrono::microseconds(s.monoMicros);
    const In in(s);
    visit(in, f);
    for (const auto& t : s.procs) {
        ProcessRecord r;
        r.key = {t.pid, t.startTicks};
        r.name = t.name;
        r.cpu = t.cpu;
        r.memFootprint = t.mem;
        r.gpu = t.gpu;
        r.diskReadBps = t.diskRead;
        r.diskWriteBps = t.diskWrite;
        f.processes.processes.push_back(std::move(r));
    }
    return f;
}

}  // namespace pmp::trace
