// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/player.hpp>

#include "header.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>

namespace pmp::trace {
namespace {

constexpr std::size_t kCacheChunks = 4;

bool hasFooter(std::span<const std::byte> b, std::size_t headerBytes) {
    if (b.size() < headerBytes + 16) return false;
    return std::memcmp(b.data() + b.size() - 8, kFooterMagic, 8) == 0;
}

}  // namespace

OpenResult Player::open(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return {"Could not open " + path.string() + ": " + ec.message(), false, {}};
    if (size > kMaxFileBytes) return {"This recording is larger than 256 MB, which this version cannot open.", false, {}};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {"Could not open " + path.string() + ": " + std::strerror(errno), false, {}};
    Bytes buf(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!in && !buf.empty()) return {"Could not read " + path.string() + ".", false, {}};
    return openBytes(buf);
}

OpenResult Player::openBytes(std::span<const std::byte> bytes) {
    *this = Player{};
    if (bytes.size() > kMaxFileBytes) return {"This recording is larger than 256 MB, which this version cannot open.", false, {}};
    const auto h = parseHeader(bytes);
    switch (h.status) {
        case HeaderStatus::NotATrace: return {"This is not a PerfMonPlus recording.", false, {}};
        case HeaderStatus::NewerVersion:
            return {"This recording was made by a newer version of PerfMonPlus and cannot be opened by this one.", false, {}};
        case HeaderStatus::Corrupt: return {"This recording's header is damaged, so it cannot be opened.", false, {}};
        case HeaderStatus::Ok: break;
    }
    bytes_.assign(bytes.begin(), bytes.end());
    info_ = h.info;

    // Walk the chunk records. Each is verified by its checksum; the footer is only needed to tell a complete file
    // from one that was cut off.
    ByteReader r(bytes_);
    r.take(h.bytes);
    int damaged = 0;
    bool cutOff = false, afterDamage = false;
    while (r.remaining() >= 4) {
        const auto at = r.position();
        const auto tag = r.take(4);
        if (std::memcmp(tag.data(), "CHNK", 4) != 0) {
            if (std::memcmp(tag.data(), "INDX", 4) != 0) cutOff = true;  // neither a chunk nor the index: stop reading
            break;
        }
        const auto inflated = r.u32(), deflated = r.u32(), count = r.u32();
        const auto firstMs = r.i64();
        if (r.failed() || deflated > r.remaining() || r.remaining() - deflated < 4) {
            cutOff = true;  // the record runs past the end of the file
            break;
        }
        const auto payloadAt = r.position();
        const auto payload = r.take(deflated);
        const auto crc = r.u32();
        (void)at;
        std::optional<std::vector<TraceSample>> samples;
        if (crc == crc32(payload) && count > 0 && count <= kMaxSamplesPerChunk) {
            samples = decodeChunk(payload, inflated, count, firstMs);
        }
        if (!samples || samples->empty()) {
            ++damaged;  // its samples are dropped; the next good one is marked as following a gap
            afterDamage = true;
            continue;
        }
        ChunkRef ref;
        ref.offset = payloadAt;
        ref.inflatedLen = inflated;
        ref.deflatedLen = deflated;
        ref.samples = count;
        ref.firstWallMs = firstMs;
        ref.firstIndex = wall_.size();
        chunks_.push_back(ref);
        for (std::size_t k = 0; k < samples->size(); ++k) {
            wall_.push_back((*samples)[k].wall);
            gap_.push_back((*samples)[k].gapBefore || (k == 0 && afterDamage));
        }
        afterDamage = false;
    }
    if (wall_.empty()) {
        *this = Player{};
        return {"This recording contains no readable data.", false, {}};
    }
    info_.sampleCount = wall_.size();
    info_.duration = std::chrono::duration_cast<std::chrono::milliseconds>(wall_.back() - wall_.front());

    OpenResult out;
    const bool complete = !cutOff && hasFooter(bytes_, h.bytes);
    if (!complete || damaged > 0) {
        out.recovered = true;
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(info_.duration).count();
        if (!complete) out.note = "Recovered " + std::to_string(secs) + " seconds; the end of the file was missing.";
        if (damaged > 0) {
            if (!out.note.empty()) out.note += " ";
            out.note += std::to_string(damaged) + (damaged == 1 ? " damaged section skipped." : " damaged sections skipped.");
        }
    }
    return out;
}

std::chrono::system_clock::time_point Player::wallAt(std::size_t i) const { return wall_.at(i); }

bool Player::gapBefore(std::size_t i) const { return gap_.at(i); }

const std::vector<TraceSample>* Player::decoded(std::size_t chunk) const {
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->first == chunk) {
            cache_.splice(cache_.begin(), cache_, it);
            return &cache_.front().second;
        }
    }
    const auto& c = chunks_[chunk];
    auto samples = decodeChunk(std::span<const std::byte>(bytes_).subspan(c.offset, c.deflatedLen), c.inflatedLen, c.samples,
                               c.firstWallMs);
    if (!samples) return nullptr;
    cache_.emplace_front(chunk, std::move(*samples));
    if (cache_.size() > kCacheChunks) cache_.pop_back();
    return &cache_.front().second;
}

std::optional<TraceSample> Player::sample(std::size_t i) const {
    if (i >= wall_.size()) return std::nullopt;
    const auto it = std::upper_bound(chunks_.begin(), chunks_.end(), i, [](std::size_t idx, const ChunkRef& c) { return idx < c.firstIndex; });
    const auto chunk = static_cast<std::size_t>(it - chunks_.begin()) - 1;
    const auto* s = decoded(chunk);
    if (!s) return std::nullopt;
    const auto k = i - chunks_[chunk].firstIndex;
    if (k >= s->size()) return std::nullopt;
    auto out = (*s)[k];
    out.gapBefore = gap_[i];
    return out;
}

std::optional<Frame> Player::frame(std::size_t i) const {
    const auto s = sample(i);
    if (!s) return std::nullopt;
    return unflatten(*s, i + 1);
}

std::size_t Player::indexAtOrBefore(std::chrono::system_clock::time_point t) const {
    if (wall_.empty()) return 0;
    const auto it = std::upper_bound(wall_.begin(), wall_.end(), t);
    return it == wall_.begin() ? 0 : static_cast<std::size_t>(it - wall_.begin()) - 1;
}

}  // namespace pmp::trace
