// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/recorder.hpp>

#include <perfmonplus/core/version.hpp>

#include "header.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace pmp::trace {
namespace {

std::int64_t wallMsOf(const Frame& f) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(f.wall.time_since_epoch()).count();
}

}  // namespace

Recorder::Recorder(HostMeta host, std::chrono::milliseconds interval, RecorderOptions options)
    : host_(std::move(host)), interval_(interval), opt_(std::move(options)) {
    if (opt_.samplesPerChunk < 1) opt_.samplesPerChunk = 1;
    if (opt_.app.empty()) opt_.app = "PerfMonPlus " + std::string(versionString());
}

void Recorder::setRing(std::optional<std::chrono::minutes> window) {
    opt_.ring = window;
    dropOldest();
}

void Recorder::start() {
    if (state_ == State::Full) return;
    state_ = State::Recording;
}

void Recorder::stop() {
    if (state_ == State::Recording) state_ = State::Idle;
    seal();
}

void Recorder::clear() {
    sealed_.clear();
    open_.clear();
    samplesSealed_ = 0;
    firstWallMs_ = 0;
    state_ = State::Idle;
}

void Recorder::seal() {
    if (open_.empty()) return;
    sealed_.push_back(encodeChunk(open_));
    samplesSealed_ += open_.size();
    open_.clear();
}

void Recorder::dropOldest() {
    if (!opt_.ring) return;
    const auto window = std::chrono::duration_cast<std::chrono::milliseconds>(*opt_.ring).count();
    // Keep the oldest chunk only while dropping it would leave less than the requested window.
    while (sealed_.size() > 1 && sealed_.back().lastWallMs - sealed_[1].firstWallMs >= window) {
        samplesSealed_ -= sealed_.front().samples;
        sealed_.erase(sealed_.begin());
    }
}

void Recorder::append(const Frame& frame, bool gapBefore) {
    if (state_ != State::Recording) return;
    const auto wall = wallMsOf(frame);
    if (sampleCount() == 0) firstWallMs_ = wall;
    if (!opt_.ring && wall - firstWallMs_ >= std::chrono::duration_cast<std::chrono::milliseconds>(opt_.maxLength).count()) {
        seal();
        state_ = State::Full;
        return;
    }
    open_.push_back(flatten(frame, gapBefore, opt_.topProcesses));
    if (static_cast<int>(open_.size()) >= opt_.samplesPerChunk) {
        seal();
        dropOldest();
    }
}

std::size_t Recorder::sampleCount() const { return samplesSealed_ + open_.size(); }

std::chrono::milliseconds Recorder::elapsed() const {
    if (sampleCount() == 0) return std::chrono::milliseconds(0);
    const auto first = !sealed_.empty() ? sealed_.front().firstWallMs : std::chrono::duration_cast<std::chrono::milliseconds>(open_.front().wall.time_since_epoch()).count();
    const auto last = !open_.empty() ? std::chrono::duration_cast<std::chrono::milliseconds>(open_.back().wall.time_since_epoch()).count() : sealed_.back().lastWallMs;
    return std::chrono::milliseconds(last - first);
}

std::size_t Recorder::compressedBytes() const {
    std::size_t total = 0;
    for (const auto& c : sealed_) total += c.record.size();
    if (!open_.empty() && !sealed_.empty()) {
        const auto& last = sealed_.back();
        total += last.record.size() * open_.size() / std::max<std::size_t>(1, last.samples);
    }
    return total;
}

std::size_t Recorder::estimatedBytesPerMinute() const {
    if (sealed_.empty()) return 0;
    const auto n = std::min<std::size_t>(sealed_.size(), 10);
    std::size_t bytes = 0, samples = 0;
    for (std::size_t i = sealed_.size() - n; i < sealed_.size(); ++i) {
        bytes += sealed_[i].record.size();
        samples += sealed_[i].samples;
    }
    if (samples == 0) return 0;
    const double perSample = static_cast<double>(bytes) / static_cast<double>(samples);
    return static_cast<std::size_t>(perSample * 60000.0 / static_cast<double>(interval_.count()));
}

std::vector<std::byte> Recorder::serialise(std::optional<std::chrono::minutes> lastMinutes) const {
    std::vector<EncodedChunk> chunks = sealed_;
    if (!open_.empty()) chunks.push_back(encodeChunk(open_));  // the open chunk is included without being sealed
    if (lastMinutes && !chunks.empty()) {
        const auto want = std::chrono::duration_cast<std::chrono::milliseconds>(*lastMinutes).count();
        const auto end = chunks.back().lastWallMs;
        std::size_t from = chunks.size();
        while (from > 0) {
            --from;
            if (end - chunks[from].firstWallMs >= want) break;  // this chunk starts early enough: take it and stop
        }
        chunks.erase(chunks.begin(), chunks.begin() + static_cast<long>(from));
    }

    TraceInfo info;
    info.app = opt_.app;
    info.host = host_;
    info.startWallMs = chunks.empty() ? 0 : chunks.front().firstWallMs;
    info.intervalMs = static_cast<int>(interval_.count());
    info.topProcesses = opt_.topProcesses;

    ByteWriter w;
    w.bytes(encodeHeader(info));
    struct Entry {
        std::uint64_t offset;
        std::int64_t firstWallMs;
        std::uint32_t samples;
    };
    std::vector<Entry> index;
    for (const auto& c : chunks) {
        index.push_back({w.size(), c.firstWallMs, c.samples});
        w.bytes(c.record);
    }
    const auto indexOffset = w.size();
    ByteWriter ix;
    ix.u8('I'); ix.u8('N'); ix.u8('D'); ix.u8('X');
    ix.u32(static_cast<std::uint32_t>(index.size()));
    for (const auto& e : index) {
        ix.u64(e.offset);
        ix.i64(e.firstWallMs);
        ix.u32(e.samples);
    }
    ix.u32(crc32(ix.data()));
    w.bytes(ix.data());
    w.u64(indexOffset);
    for (int i = 0; i < 8; ++i) w.u8(static_cast<std::uint8_t>(kFooterMagic[i]));
    return w.take();
}

bool Recorder::save(const std::filesystem::path& path, std::string* error, std::optional<std::chrono::minutes> lastMinutes) const {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (sampleCount() == 0) return fail("There is nothing recorded to save.");
    const auto bytes = serialise(lastMinutes);
    auto partial = path;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) return fail("Could not write " + path.string() + ": " + std::strerror(errno));
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            const std::string why = std::strerror(errno);
            std::error_code ec;
            std::filesystem::remove(partial, ec);
            return fail("Could not write " + path.string() + ": " + why);
        }
    }
    std::error_code ec;
    std::filesystem::rename(partial, path, ec);
    if (ec) {
        std::error_code ignore;
        std::filesystem::remove(partial, ignore);
        return fail("Could not save " + path.string() + ": " + ec.message());
    }
    return true;
}

}  // namespace pmp::trace
