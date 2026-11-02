// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/container.hpp>

#include <zlib.h>

#include <array>
#include <bit>
#include <cstring>

namespace pmp::trace {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

}  // namespace

std::uint32_t crc32(std::span<const std::byte> data, std::uint32_t seed) {
    std::uint32_t c = ~seed;
    const auto& t = crcTable();
    for (const auto b : data) c = t[(c ^ static_cast<std::uint8_t>(b)) & 0xFF] ^ (c >> 8);
    return ~c;
}

Bytes shuffle8(std::span<const std::byte> in) {
    const std::size_t words = in.size() / 8;
    Bytes out(in.size());
    for (std::size_t b = 0; b < 8; ++b) {
        for (std::size_t w = 0; w < words; ++w) out[b * words + w] = in[w * 8 + b];
    }
    for (std::size_t i = words * 8; i < in.size(); ++i) out[i] = in[i];
    return out;
}

Bytes unshuffle8(std::span<const std::byte> in) {
    const std::size_t words = in.size() / 8;
    Bytes out(in.size());
    for (std::size_t b = 0; b < 8; ++b) {
        for (std::size_t w = 0; w < words; ++w) out[w * 8 + b] = in[b * words + w];
    }
    for (std::size_t i = words * 8; i < in.size(); ++i) out[i] = in[i];
    return out;
}

Bytes deflateBytes(std::span<const std::byte> data) {
    uLongf cap = compressBound(static_cast<uLong>(data.size()));
    Bytes out(cap);
    const int rc = compress2(reinterpret_cast<Bytef*>(out.data()), &cap, reinterpret_cast<const Bytef*>(data.data()),
                             static_cast<uLong>(data.size()), 3);
    if (rc != Z_OK) return {};
    out.resize(cap);
    return out;
}

std::optional<Bytes> inflateBytes(std::span<const std::byte> data, std::size_t expectedLen) {
    if (expectedLen > kMaxInflatedBytes) return std::nullopt;
    Bytes out(expectedLen);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return std::nullopt;
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&zs, Z_FINISH);
    const bool ok = rc == Z_STREAM_END && zs.total_out == expectedLen && zs.avail_in == 0;
    inflateEnd(&zs);
    if (!ok) return std::nullopt;
    return out;
}

void ByteWriter::u8(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }
void ByteWriter::u16(std::uint16_t v) {
    for (int i = 0; i < 2; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i)));
}
void ByteWriter::u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i)));
}
void ByteWriter::u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i)));
}
void ByteWriter::f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
void ByteWriter::varint(std::uint64_t v) {
    while (v >= 0x80) {
        u8(static_cast<std::uint8_t>(v | 0x80));
        v >>= 7;
    }
    u8(static_cast<std::uint8_t>(v));
}
void ByteWriter::zigzag(std::int64_t v) {
    varint((static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63));
}
void ByteWriter::bytes(std::span<const std::byte> b) { buf_.insert(buf_.end(), b.begin(), b.end()); }
void ByteWriter::str(std::string_view s) {
    varint(s.size());
    for (const char c : s) u8(static_cast<std::uint8_t>(c));
}

std::span<const std::byte> ByteReader::take(std::size_t n) {
    if (failed_ || n > remaining()) {
        failed_ = true;
        return {};
    }
    const auto s = data_.subspan(pos_, n);
    pos_ += n;
    return s;
}

std::uint8_t ByteReader::u8() {
    const auto s = take(1);
    return s.empty() ? 0 : static_cast<std::uint8_t>(s[0]);
}
std::uint16_t ByteReader::u16() {
    const auto s = take(2);
    if (s.empty()) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(s[0]) | static_cast<std::uint8_t>(s[1]) << 8);
}
std::uint32_t ByteReader::u32() {
    const auto s = take(4);
    if (s.empty()) return 0;
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = v << 8 | static_cast<std::uint8_t>(s[static_cast<std::size_t>(i)]);
    return v;
}
std::uint64_t ByteReader::u64() {
    const auto s = take(8);
    if (s.empty()) return 0;
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | static_cast<std::uint8_t>(s[static_cast<std::size_t>(i)]);
    return v;
}
double ByteReader::f64() { return std::bit_cast<double>(u64()); }
std::uint64_t ByteReader::varint() {
    std::uint64_t v = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        const auto s = take(1);
        if (s.empty()) return 0;
        const auto b = static_cast<std::uint8_t>(s[0]);
        if (shift == 63 && (b & 0x7E)) {  // would overflow 64 bits
            failed_ = true;
            return 0;
        }
        v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return v;
    }
    failed_ = true;  // more than 10 bytes
    return 0;
}
std::int64_t ByteReader::zigzag() {
    const auto u = varint();
    return static_cast<std::int64_t>(u >> 1) ^ -static_cast<std::int64_t>(u & 1);
}
std::string ByteReader::str() {
    const auto n = varint();
    if (failed_ || n > remaining()) {
        failed_ = true;
        return {};
    }
    const auto s = take(static_cast<std::size_t>(n));
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

}  // namespace pmp::trace
