// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

// Byte-level building blocks of the .pmptrace container (docs/trace-format.md). All multi-byte integers are
// little-endian whatever the host is, so a trace written on one machine reads on any other.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pmp::trace {

using Bytes = std::vector<std::byte>;

// The largest inflated chunk a reader will allocate. A hostile length field can never make us allocate more.
inline constexpr std::size_t kMaxInflatedBytes = 64u << 20;

std::uint32_t crc32(std::span<const std::byte> data, std::uint32_t seed = 0);

// Byte-shuffle by 8: every word's byte 0 first, then every byte 1, ... then the leftover tail bytes unchanged.
// Doubles that change little between samples then become long runs of equal bytes that deflate well.
Bytes shuffle8(std::span<const std::byte> data);
Bytes unshuffle8(std::span<const std::byte> data);

Bytes deflateBytes(std::span<const std::byte> data);
// nullopt on any zlib error, a truncated stream, trailing data, or a length different from `expectedLen`.
std::optional<Bytes> inflateBytes(std::span<const std::byte> data, std::size_t expectedLen);

class ByteWriter {
public:
    void u8(std::uint8_t v);
    void u16(std::uint16_t v);
    void u32(std::uint32_t v);
    void u64(std::uint64_t v);
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f64(double v);
    void varint(std::uint64_t v);
    void zigzag(std::int64_t v);
    void bytes(std::span<const std::byte> b);
    void str(std::string_view s);  // varint length + bytes
    const Bytes& data() const { return buf_; }
    Bytes take() { return std::move(buf_); }
    std::size_t size() const { return buf_.size(); }

private:
    Bytes buf_;
};

// Reads never go past the end: a short read sets a sticky failed() flag and returns zero.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> data) : data_(data) {}
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    double f64();
    std::uint64_t varint();  // at most 10 bytes
    std::int64_t zigzag();
    std::span<const std::byte> take(std::size_t n);  // empty and failed() when fewer than n remain
    std::string str();
    std::size_t remaining() const { return data_.size() - pos_; }
    std::size_t position() const { return pos_; }
    bool failed() const { return failed_; }

private:
    std::span<const std::byte> data_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace pmp::trace
