// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/trace/chunk.hpp>
#include <perfmonplus/trace/trace_info.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pmp::trace {

struct RecorderOptions {
    int topProcesses = 20;
    std::chrono::minutes maxLength{30};         // hard cap: the recorder reports Full and stops accepting
    std::optional<std::chrono::minutes> ring;   // engaged: keep only the last N minutes (the "black box", FLR-07)
    int samplesPerChunk = 30;
    std::string app;                            // "PerfMonPlus x.y.z"; empty = the current version
};

// Flight Recorder (§8.2): turns frames into compressed chunks held in memory, and writes them as a .pmptrace file.
// Not thread-safe: call it from one thread (the one that receives frames).
class Recorder {
public:
    enum class State { Idle, Recording, Full };

    Recorder(HostMeta host, std::chrono::milliseconds interval, RecorderOptions options = {});

    // Settings that may change while recording without losing what was recorded.
    void setInterval(std::chrono::milliseconds interval) { interval_ = interval; }
    void setRing(std::optional<std::chrono::minutes> window);  // a smaller window drops the oldest chunks at once
    void setMaxLength(std::chrono::minutes length) { opt_.maxLength = length; }
    void setTopProcesses(int n) { opt_.topProcesses = n; }  // applies to samples from now on
    void setHost(HostMeta host) { host_ = std::move(host); }  // takes effect in the next serialise()
    void start();                                // Idle -> Recording; in ring mode earlier samples are kept
    void append(const Frame& frame, bool gapBefore);  // ignored unless Recording
    void stop();                                 // seals the open chunk; samples stay available to save
    void clear();                                // forgets everything
    State state() const { return state_; }
    std::size_t sampleCount() const;
    std::chrono::milliseconds elapsed() const;   // from the recorded wall times, not a timer
    std::size_t compressedBytes() const;         // sealed chunks plus an estimate for the open one
    std::size_t estimatedBytesPerMinute() const; // from the most recent sealed chunks; 0 until one exists

    // The whole recording, or only its last `lastMinutes` (rounded up to whole chunks, never fewer than asked).
    std::vector<std::byte> serialise(std::optional<std::chrono::minutes> lastMinutes = {}) const;
    // Writes <path>.partial then renames it, so a failed save never leaves half a trace under the real name.
    bool save(const std::filesystem::path& path, std::string* error,
              std::optional<std::chrono::minutes> lastMinutes = {}) const;

private:
    void seal();
    void dropOldest();

    HostMeta host_;
    std::chrono::milliseconds interval_;
    RecorderOptions opt_;
    State state_ = State::Idle;
    std::vector<EncodedChunk> sealed_;
    std::vector<TraceSample> open_;
    std::int64_t firstWallMs_ = 0;  // wall time of the first sample ever accepted (for the max-length cap)
    std::size_t samplesSealed_ = 0;
};

}  // namespace pmp::trace
