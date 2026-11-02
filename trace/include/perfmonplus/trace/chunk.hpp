// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

// One chunk of a .pmptrace file: a run of consecutive samples, encoded column by column (docs/trace-format.md).

#include <perfmonplus/trace/container.hpp>
#include <perfmonplus/trace/frame_codec.hpp>

#include <optional>
#include <span>
#include <vector>

namespace pmp::trace {

inline constexpr std::uint32_t kMaxSamplesPerChunk = 100'000;
inline constexpr std::size_t kChunkHeaderBytes = 4 + 4 + 4 + 4 + 8;  // "CHNK" inflatedLen deflatedLen samples firstWallMs

struct EncodedChunk {
    Bytes record;                  // the whole chunk as it appears in the file: header, deflated payload, CRC
    std::int64_t firstWallMs = 0;
    std::int64_t lastWallMs = 0;
    std::uint32_t samples = 0;
};

EncodedChunk encodeChunk(std::span<const TraceSample> samples);

// `payload` is the deflated bytes of a chunk record. nullopt on any inconsistency (never reads or allocates
// beyond what the bytes and kMaxInflatedBytes allow).
std::optional<std::vector<TraceSample>> decodeChunk(std::span<const std::byte> payload, std::uint32_t inflatedLen,
                                                    std::uint32_t sampleCount, std::int64_t firstWallMs);

}  // namespace pmp::trace
