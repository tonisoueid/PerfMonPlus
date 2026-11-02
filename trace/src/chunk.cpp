// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/chunk.hpp>

#include <algorithm>
#include <bit>
#include <map>
#include <unordered_map>

namespace pmp::trace {
namespace {

constexpr std::uint8_t kAbsent = 255;

std::int64_t wallMs(const TraceSample& s) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(s.wall.time_since_epoch()).count();
}

struct Strings {
    std::vector<std::string> list;
    std::unordered_map<std::string, std::uint64_t> index;
    std::uint64_t id(const std::string& s) {
        const auto [it, inserted] = index.emplace(s, list.size());
        if (inserted) list.push_back(s);
        return it->second;
    }
};

void writeMetric(ByteWriter& w, const Metric& m) {
    w.u8(static_cast<std::uint8_t>(m.availability));
    w.f64(m.isMeasured() && m.value ? *m.value : 0.0);
}

Metric readMetric(ByteReader& r, Unit unit) {
    const auto a = r.u8();
    const auto v = r.f64();
    if (a > static_cast<std::uint8_t>(Availability::Disabled)) return Metric::notMeasured(Availability::Failed, unit);
    const auto avail = static_cast<Availability>(a);
    return avail == Availability::Measured ? Metric::measured(v, unit) : Metric::notMeasured(avail, unit);
}

}  // namespace

EncodedChunk encodeChunk(std::span<const TraceSample> samples) {
    EncodedChunk out;
    const auto n = samples.size();
    out.samples = static_cast<std::uint32_t>(n);
    out.firstWallMs = n ? wallMs(samples.front()) : 0;
    out.lastWallMs = n ? wallMs(samples.back()) : 0;

    // Series and text tables: ids in order of first appearance.
    std::vector<std::pair<std::string, Unit>> seriesIds;
    std::map<std::string, std::size_t> seriesIndex;
    std::vector<std::string> textIds;
    std::map<std::string, std::size_t> textIndex;
    for (const auto& s : samples) {
        for (const auto& v : s.values) {
            if (seriesIndex.emplace(v.id, seriesIds.size()).second) seriesIds.emplace_back(v.id, v.unit);
        }
        for (const auto& t : s.texts) {
            if (textIndex.emplace(t.id, textIds.size()).second) textIds.push_back(t.id);
        }
    }
    Strings strings;
    std::vector<std::vector<std::uint64_t>> textCols(textIds.size(), std::vector<std::uint64_t>(n, 0));
    for (std::size_t k = 0; k < n; ++k) {
        for (const auto& t : samples[k].texts) textCols[textIndex[t.id]][k] = strings.id(t.value) + 1;
        for (const auto& p : samples[k].procs) strings.id(p.name);
    }

    ByteWriter w;
    w.varint(seriesIds.size());
    for (const auto& [id, unit] : seriesIds) {
        w.str(id);
        w.u8(static_cast<std::uint8_t>(unit));
    }
    w.varint(textIds.size());
    for (const auto& id : textIds) w.str(id);
    w.varint(strings.list.size());
    for (const auto& s : strings.list) w.str(s);

    std::int64_t prevMono = 0, prevWall = out.firstWallMs;
    for (const auto& s : samples) {
        w.zigzag(s.monoMicros - prevMono);
        w.zigzag(wallMs(s) - prevWall);
        w.u8(s.gapBefore ? 1 : 0);
        prevMono = s.monoMicros;
        prevWall = wallMs(s);
    }

    // Availability, then text indexes, series-major.
    std::vector<std::vector<const SeriesValue*>> cols(seriesIds.size(), std::vector<const SeriesValue*>(n, nullptr));
    for (std::size_t k = 0; k < n; ++k) {
        for (const auto& v : samples[k].values) cols[seriesIndex[v.id]][k] = &v;
    }
    for (const auto& col : cols) {
        for (const auto* v : col) w.u8(v ? static_cast<std::uint8_t>(v->avail) : kAbsent);
    }
    for (const auto& col : textCols) {
        for (const auto idx : col) w.varint(idx);
    }

    // Top processes.
    for (const auto& s : samples) {
        w.varint(s.procs.size());
        for (const auto& p : s.procs) {
            w.varint(static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.pid)));
            w.varint(p.startTicks);
            w.varint(strings.id(p.name));
            writeMetric(w, p.cpu);
            writeMetric(w, p.mem);
            writeMetric(w, p.gpu);
            writeMetric(w, p.diskRead);
            writeMetric(w, p.diskWrite);
        }
    }

    // Values: each measured value XOR-ed with the series' previous measured value, series-major, then byte-shuffled
    // so the high bytes (mostly zero when a series is steady) form long runs. Unmeasured slots cost a zero word and
    // do not move the "previous" value.
    ByteWriter vals;
    for (const auto& col : cols) {
        std::uint64_t prev = 0;
        for (const auto* v : col) {
            std::uint64_t x = 0;
            if (v && v->avail == Availability::Measured) {
                const auto bits = std::bit_cast<std::uint64_t>(v->value);
                x = bits ^ prev;
                prev = bits;
            }
            vals.u64(x);
        }
    }
    w.bytes(shuffle8(vals.data()));

    const auto inflatedLen = static_cast<std::uint32_t>(w.size());
    const auto deflated = deflateBytes(w.data());
    ByteWriter rec;
    rec.u8('C'); rec.u8('H'); rec.u8('N'); rec.u8('K');
    rec.u32(inflatedLen);
    rec.u32(static_cast<std::uint32_t>(deflated.size()));
    rec.u32(out.samples);
    rec.i64(out.firstWallMs);
    rec.bytes(deflated);
    rec.u32(crc32(deflated));
    out.record = rec.take();
    return out;
}

std::optional<std::vector<TraceSample>> decodeChunk(std::span<const std::byte> payload, std::uint32_t inflatedLen,
                                                    std::uint32_t sampleCount, std::int64_t firstWallMs) {
    if (sampleCount > kMaxSamplesPerChunk) return std::nullopt;
    const auto inflated = inflateBytes(payload, inflatedLen);
    if (!inflated) return std::nullopt;
    ByteReader r(*inflated);
    const std::size_t n = sampleCount;
    auto fits = [&](std::uint64_t count, std::size_t perElementBytes) { return count <= r.remaining() / std::max<std::size_t>(1, perElementBytes); };

    const auto nSeries = r.varint();
    if (r.failed() || !fits(nSeries, 2)) return std::nullopt;
    std::vector<std::pair<std::string, Unit>> series;
    for (std::uint64_t i = 0; i < nSeries; ++i) {
        auto id = r.str();
        const auto u = r.u8();
        if (u > static_cast<std::uint8_t>(Unit::WattHours)) return std::nullopt;
        series.emplace_back(std::move(id), static_cast<Unit>(u));
    }
    const auto nTexts = r.varint();
    if (r.failed() || !fits(nTexts, 1)) return std::nullopt;
    std::vector<std::string> textIds;
    for (std::uint64_t i = 0; i < nTexts; ++i) textIds.push_back(r.str());
    const auto nStrings = r.varint();
    if (r.failed() || !fits(nStrings, 1)) return std::nullopt;
    std::vector<std::string> strings;
    for (std::uint64_t i = 0; i < nStrings; ++i) strings.push_back(r.str());
    if (r.failed()) return std::nullopt;

    std::vector<TraceSample> out(n);
    std::int64_t mono = 0, wall = firstWallMs;
    for (auto& s : out) {
        mono += r.zigzag();
        wall += r.zigzag();
        s.monoMicros = mono;
        s.wall = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(wall);
        s.gapBefore = (r.u8() & 1) != 0;
    }
    if (r.failed()) return std::nullopt;

    std::vector<std::vector<std::uint8_t>> avail(series.size());
    for (auto& col : avail) {
        if (n > r.remaining()) return std::nullopt;
        for (std::size_t k = 0; k < n; ++k) col.push_back(r.u8());
    }
    std::vector<std::vector<std::uint64_t>> textCols(textIds.size());
    for (auto& col : textCols) {
        if (n > r.remaining()) return std::nullopt;
        for (std::size_t k = 0; k < n; ++k) {
            const auto idx = r.varint();
            if (idx > strings.size()) return std::nullopt;
            col.push_back(idx);
        }
    }
    if (r.failed()) return std::nullopt;

    for (auto& s : out) {
        const auto np = r.varint();
        if (r.failed() || !fits(np, 40)) return std::nullopt;
        for (std::uint64_t i = 0; i < np; ++i) {
            TopProcess p;
            p.pid = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.varint()));
            p.startTicks = r.varint();
            const auto name = r.varint();
            if (name >= strings.size()) return std::nullopt;
            p.name = strings[name];
            p.cpu = readMetric(r, Unit::Percent);
            p.mem = readMetric(r, Unit::Bytes);
            p.gpu = readMetric(r, Unit::Percent);
            p.diskRead = readMetric(r, Unit::BytesPerSec);
            p.diskWrite = readMetric(r, Unit::BytesPerSec);
            s.procs.push_back(std::move(p));
        }
    }
    if (r.failed()) return std::nullopt;

    const std::size_t valueBytes = series.size() * n * 8;
    if (r.remaining() != valueBytes) return std::nullopt;
    const auto vals = unshuffle8(r.take(valueBytes));
    ByteReader vr(vals);
    for (std::size_t si = 0; si < series.size(); ++si) {
        std::uint64_t prev = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const auto x = vr.u64();
            const auto a = avail[si][k];
            if (a == kAbsent) continue;
            if (a > static_cast<std::uint8_t>(Availability::Disabled)) return std::nullopt;
            SeriesValue v;
            v.id = series[si].first;
            v.unit = series[si].second;
            v.avail = static_cast<Availability>(a);
            if (v.avail == Availability::Measured) {
                prev ^= x;
                v.value = std::bit_cast<double>(prev);
            }
            out[k].values.push_back(std::move(v));
        }
    }
    for (std::size_t ti = 0; ti < textIds.size(); ++ti) {
        for (std::size_t k = 0; k < n; ++k) {
            if (textCols[ti][k] != 0) out[k].texts.push_back({textIds[ti], strings[textCols[ti][k] - 1]});
        }
    }
    return out;
}

}  // namespace pmp::trace
