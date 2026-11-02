// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/trace/chunk.hpp>
#include <perfmonplus/trace/trace_info.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pmp::trace {

struct OpenResult {
    std::string error;        // "" on success; otherwise one sentence a person can act on
    bool recovered = false;   // the file was cut short or partly damaged; what could be read is available
    std::string note;         // what was recovered or skipped, for the replay banner
    bool ok() const { return error.empty(); }
};

// Reads a .pmptrace file (§8.2 FLR-05). Opening reads the whole file once (at most 256 MiB), verifies every
// chunk's checksum, and keeps a small timing table; chunks are decoded again on demand (four are cached).
// A cut-off file replays up to its last complete chunk; a damaged chunk becomes a gap (INT-03), never a guess.
class Player {
public:
    OpenResult open(const std::filesystem::path& path);
    OpenResult openBytes(std::span<const std::byte> bytes);

    const TraceInfo& info() const { return info_; }
    std::size_t sampleCount() const { return wall_.size(); }
    std::chrono::system_clock::time_point wallAt(std::size_t i) const;
    bool gapBefore(std::size_t i) const;
    std::optional<TraceSample> sample(std::size_t i) const;
    std::optional<Frame> frame(std::size_t i) const;  // unflatten(sample(i), i + 1)
    // The last sample not after `t`; 0 before the first, the last sample after the end. 0 when empty.
    std::size_t indexAtOrBefore(std::chrono::system_clock::time_point t) const;

    static constexpr std::size_t kMaxFileBytes = 256u << 20;

private:
    struct ChunkRef {
        std::size_t offset = 0;  // of the deflated payload within bytes_
        std::uint32_t inflatedLen = 0, deflatedLen = 0, samples = 0;
        std::int64_t firstWallMs = 0;
        std::size_t firstIndex = 0;  // index of the chunk's first sample in the whole trace
    };
    const std::vector<TraceSample>* decoded(std::size_t chunk) const;

    Bytes bytes_;
    TraceInfo info_;
    std::vector<ChunkRef> chunks_;
    std::vector<std::chrono::system_clock::time_point> wall_;
    std::vector<bool> gap_;
    mutable std::list<std::pair<std::size_t, std::vector<TraceSample>>> cache_;
};

}  // namespace pmp::trace
