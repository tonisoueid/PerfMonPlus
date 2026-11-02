// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/trace/chunk.hpp>
#include <perfmonplus/trace/recorder.hpp>

#include "../../trace/src/header.hpp"
#include "support/full_frame.hpp"

#include <random>

namespace pmp::testing {

// A frame like fullFrame() whose values drift a little each second, the way a real machine's do.
inline Frame noisyFrame(int second, std::uint32_t seed = 1, int processes = 60) {
    std::mt19937 rng(seed + static_cast<std::uint32_t>(second) * 7919u);
    std::uniform_real_distribution<double> small(-1.0, 1.0);
    auto f = fullFrame(processes);
    f.mono = MonoTime{} + std::chrono::seconds(second);
    f.wall = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(1'700'000'000'000 + second * 1000LL);
    auto jitter = [&](Metric& m, double scale) {
        if (m.isMeasured()) m = Metric::measured(*m.value + small(rng) * scale, m.unit);
    };
    jitter(f.cpu.total, 8);
    jitter(f.cpu.kernel, 2);
    for (auto& l : f.cpu.logical) {
        jitter(l.util, 15);
        jitter(l.freqHz, 2e8);
    }
    jitter(f.memory.inUse, 4e6);
    jitter(f.disks.combined.readBps, 1e5);
    jitter(f.net.combined.rxBps, 5e4);
    jitter(f.thermals.hotspot, 1.5);
    for (auto& s : f.thermals.sensors) jitter(s.celsius, 1.0);
    for (auto& p : f.processes.processes) {
        jitter(p.cpu, 3);
        jitter(p.memFootprint, 2e5);
        jitter(p.diskReadBps, 1e4);
    }
    return f;
}

struct Decoded {
    trace::TraceInfo info;
    std::vector<trace::TraceSample> samples;
    bool ok = false;
};

// Walks a serialised trace chunk by chunk (independent of the Player).
inline Decoded decodeAll(std::span<const std::byte> bytes) {
    using namespace pmp::trace;
    Decoded d;
    const auto h = parseHeader(bytes);
    if (h.status != HeaderStatus::Ok) return d;
    d.info = h.info;
    ByteReader r(bytes);
    r.take(h.bytes);
    while (r.remaining() >= 4) {
        const auto tag = r.take(4);
        if (tag[0] != std::byte{'C'}) break;
        const auto inflated = r.u32(), deflated = r.u32(), count = r.u32();
        const auto first = r.i64();
        const auto payload = r.take(deflated);
        const auto crc = r.u32();
        if (r.failed() || crc != crc32(payload)) return d;
        auto s = decodeChunk(payload, inflated, count, first);
        if (!s) return d;
        for (auto& x : *s) d.samples.push_back(std::move(x));
    }
    d.ok = true;
    return d;
}

}  // namespace pmp::testing
