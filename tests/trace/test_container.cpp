// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/container.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <string_view>

using namespace pmp::trace;

namespace {

Bytes bytesOf(std::string_view s) {
    Bytes b(s.size());
    std::memcpy(b.data(), s.data(), s.size());
    return b;
}

Bytes counting(std::size_t n) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::byte>(i * 31 + 7);
    return b;
}

}  // namespace

TEST_CASE("crc32 matches the standard check value", "[trace][container]") {
    REQUIRE(crc32(bytesOf("123456789")) == 0xCBF43926u);
    REQUIRE(crc32({}) == 0u);
}

TEST_CASE("shuffle8 round-trips and groups bytes", "[trace][container]") {
    Bytes in(16);
    for (std::size_t i = 0; i < 16; ++i) in[i] = static_cast<std::byte>(i);  // two words: 0..7 and 8..15
    const auto out = shuffle8(in);
    REQUIRE(static_cast<int>(out[0]) == 0);
    REQUIRE(static_cast<int>(out[1]) == 8);  // all byte-0s first
    REQUIRE(static_cast<int>(out[2]) == 1);
    REQUIRE(static_cast<int>(out[3]) == 9);
    for (std::size_t n : {0u, 1u, 7u, 8u, 9u, 1000u}) {
        const auto data = counting(n);
        REQUIRE(unshuffle8(shuffle8(data)) == data);
    }
}

TEST_CASE("deflate round-trips", "[trace][container]") {
    const auto data = counting(5000);
    const auto packed = deflateBytes(data);
    const auto back = inflateBytes(packed, data.size());
    REQUIRE(back.has_value());
    REQUIRE(*back == data);
    const Bytes zeros(100000);
    REQUIRE(deflateBytes(zeros).size() < 1000);  // 100 KB of zeros is a few hundred bytes
}

TEST_CASE("inflate rejects a wrong expected length", "[trace][container]") {
    const auto data = counting(1000);
    const auto packed = deflateBytes(data);
    REQUIRE_FALSE(inflateBytes(packed, 999).has_value());
    REQUIRE_FALSE(inflateBytes(packed, 1001).has_value());
}

TEST_CASE("inflate rejects a truncated stream and garbage", "[trace][container]") {
    const auto packed = deflateBytes(counting(4000));
    Bytes cut(packed.begin(), packed.begin() + static_cast<long>(packed.size() / 2));
    REQUIRE_FALSE(inflateBytes(cut, 4000).has_value());
    REQUIRE_FALSE(inflateBytes(counting(64), 4000).has_value());
}

TEST_CASE("inflate never allocates beyond the cap", "[trace][container]") {
    const auto packed = deflateBytes(Bytes(1000));
    REQUIRE_FALSE(inflateBytes(packed, std::size_t{1} << 30).has_value());
    REQUIRE_FALSE(inflateBytes(packed, kMaxInflatedBytes + 1).has_value());
}

TEST_CASE("varint and zigzag round-trip", "[trace][container]") {
    ByteWriter w;
    const std::uint64_t us[] = {0, 1, 127, 128, 300, 1ull << 63, std::numeric_limits<std::uint64_t>::max()};
    const std::int64_t is[] = {0, 1, -1, 63, -64, std::numeric_limits<std::int64_t>::min(),
                               std::numeric_limits<std::int64_t>::max()};
    for (auto v : us) w.varint(v);
    for (auto v : is) w.zigzag(v);
    ByteReader r(w.data());
    for (auto v : us) REQUIRE(r.varint() == v);
    for (auto v : is) REQUIRE(r.zigzag() == v);
    REQUIRE_FALSE(r.failed());
    REQUIRE(r.remaining() == 0);
}

TEST_CASE("reader stops at the end", "[trace][container]") {
    ByteWriter w;
    w.u16(7);
    ByteReader r(w.data());
    REQUIRE(r.u32() == 0u);  // only two bytes available
    REQUIRE(r.failed());
    REQUIRE(r.u8() == 0);    // stays failed, never reads past the end
    ByteReader trunc(std::span<const std::byte>(w.data()).first(1));
    trunc.varint();
    REQUIRE_FALSE(trunc.failed());  // 0x07 fits one byte... but only one byte of u16 exists: varint reads it
    Bytes endless(20, std::byte{0xFF});
    ByteReader bad(endless);
    bad.varint();
    REQUIRE(bad.failed());  // a varint longer than 10 bytes is rejected
}

TEST_CASE("integers are little-endian", "[trace][container]") {
    ByteWriter w;
    w.u32(0x01020304u);
    w.u64(0x0102030405060708ull);
    w.i64(-2);
    const auto& d = w.data();
    REQUIRE(static_cast<int>(d[0]) == 0x04);
    REQUIRE(static_cast<int>(d[3]) == 0x01);
    REQUIRE(static_cast<int>(d[4]) == 0x08);
    REQUIRE(static_cast<int>(d[11]) == 0x01);
    REQUIRE(static_cast<int>(d[12]) == 0xFE);
    ByteReader r(d);
    REQUIRE(r.u32() == 0x01020304u);
    REQUIRE(r.u64() == 0x0102030405060708ull);
    REQUIRE(r.i64() == -2);
}

TEST_CASE("strings and doubles round-trip", "[trace][container]") {
    ByteWriter w;
    w.str("héllo");
    w.f64(-1234.5);
    ByteReader r(w.data());
    REQUIRE(r.str() == "héllo");
    REQUIRE(r.f64() == -1234.5);
    REQUIRE_FALSE(r.failed());
    ByteWriter huge;
    huge.varint(1u << 30);  // a length that is not there
    ByteReader h(huge.data());
    REQUIRE(h.str().empty());
    REQUIRE(h.failed());
}
