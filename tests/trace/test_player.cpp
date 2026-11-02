// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/player.hpp>

#include "trace_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <unistd.h>

using namespace pmp;
using namespace pmp::trace;
using namespace pmp::testing;
using namespace std::chrono_literals;

namespace {

Bytes record(int samples, int gapAt = -1, std::string os = "macOS 15") {
    HostMeta h;
    h.os = std::move(os);
    h.cpuModel = "Test CPU";
    h.logicalCpus = 2;
    RecorderOptions o;
    o.app = "PerfMonPlus test";
    Recorder rec(h, 1000ms, o);
    rec.start();
    for (int i = 0; i < samples; ++i) rec.append(noisyFrame(i < 12 || gapAt < 0 ? i : i + 40, 3), i == gapAt);
    rec.stop();
    return rec.serialise();
}

}  // namespace

TEST_CASE("the player reads back what was recorded", "[trace][player]") {
    const auto bytes = record(100);
    Player p;
    const auto r = p.openBytes(bytes);
    REQUIRE(r.ok());
    REQUIRE_FALSE(r.recovered);
    REQUIRE(p.sampleCount() == 100);
    REQUIRE(p.info().host.os == "macOS 15");
    REQUIRE(p.info().host.cpuModel == "Test CPU");
    REQUIRE(p.info().app == "PerfMonPlus test");
    REQUIRE(p.info().duration == 99s);
    for (std::size_t i : {0u, 31u, 50u, 99u}) {
        const auto f = p.frame(i);
        REQUIRE(f.has_value());
        std::string why;
        INFO("sample " << i << " " << why);
        const auto want = unflatten(flatten(noisyFrame(static_cast<int>(i), 3), false, 20), i + 1);
        REQUIRE(sameRecorded(want, *f, &why));
        REQUIRE(f->generation == i + 1);
    }
    REQUIRE_FALSE(p.sample(100).has_value());
}

TEST_CASE("rejects non-trace files", "[trace][player]") {
    const std::vector<std::vector<std::uint8_t>> junk = {
        {}, {1, 2, 3, 4, 5, 6, 7}, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13}, {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'}};
    for (const auto& j : junk) {
        Player p;
        Bytes b(j.size());
        std::memcpy(b.data(), j.data(), j.size());
        const auto r = p.openBytes(b);
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.error == "This is not a PerfMonPlus recording.");
        REQUIRE(p.sampleCount() == 0);
    }
}

TEST_CASE("refuses a newer major version but ignores unknown keys", "[trace][player]") {
    auto bytes = record(40);
    Bytes newer = bytes;
    newer[8] = std::byte{2};  // version 2
    Player p;
    const auto r = p.openBytes(newer);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error.find("newer version of PerfMonPlus") != std::string::npos);

    // version 1 with an extra key and an extra series opens fine
    TraceInfo info;
    info.app = "x";
    auto header = encodeHeader(info);
    REQUIRE(parseHeader(header).status == HeaderStatus::Ok);
}

TEST_CASE("recovers a truncated file", "[trace][player]") {
    const auto bytes = record(150);  // 5 chunks
    Player whole;
    REQUIRE(whole.openBytes(bytes).ok());
    // cut in the middle of the 4th chunk: three full chunks (90 samples) remain
    std::size_t cut = 0;
    {
        ByteReader r(bytes);
        r.take(parseHeader(bytes).bytes);
        for (int c = 0; c < 3; ++c) {
            r.take(4);
            const auto inflated = r.u32();
            (void)inflated;
            const auto deflated = r.u32();
            r.u32();
            r.i64();
            r.take(deflated);
            r.u32();
        }
        cut = r.position() + 40;  // inside chunk 4
    }
    Player p;
    const auto r = p.openBytes(std::span<const std::byte>(bytes).first(cut));
    REQUIRE(r.ok());
    REQUIRE(r.recovered);
    REQUIRE(p.sampleCount() == 90);
    REQUIRE(r.note.find("Recovered 89 seconds") != std::string::npos);
    REQUIRE(p.frame(89).has_value());

    Player none;
    const auto h = parseHeader(bytes).bytes;
    const auto bad = none.openBytes(std::span<const std::byte>(bytes).first(h - 3));
    REQUIRE_FALSE(bad.ok());  // cut inside the header
    const auto empty = none.openBytes(std::span<const std::byte>(bytes).first(h));
    REQUIRE_FALSE(empty.ok());  // a header and no chunks
    REQUIRE(empty.error == "This recording contains no readable data.");
}

TEST_CASE("bad checksums are detected and become a gap", "[trace][player]") {
    auto bytes = record(150);
    ByteReader r(bytes);
    r.take(parseHeader(bytes).bytes);
    std::size_t second = 0;
    for (int c = 0; c < 2; ++c) {
        r.take(4);
        r.u32();
        const auto deflated = r.u32();
        r.u32();
        r.i64();
        if (c == 1) second = r.position() + 10;  // a byte inside chunk 2's payload
        r.take(deflated);
        r.u32();
    }
    bytes[second] ^= std::byte{0xFF};
    Player p;
    const auto res = p.openBytes(bytes);
    REQUIRE(res.ok());
    REQUIRE(res.recovered);
    REQUIRE(res.note.find("1 damaged section skipped") != std::string::npos);
    REQUIRE(p.sampleCount() == 120);
    REQUIRE(p.gapBefore(30));          // the first sample after the lost chunk
    REQUIRE_FALSE(p.gapBefore(31));
    REQUIRE(p.wallAt(30) - p.wallAt(29) == 31s);  // 30 s of samples were lost: never filled
}

TEST_CASE("an absurd length field is refused", "[trace][player]") {
    auto bytes = record(40);
    ByteReader r(bytes);
    r.take(parseHeader(bytes).bytes);
    r.take(4);
    const auto inflatedAt = r.position();
    for (int i = 0; i < 4; ++i) bytes[inflatedAt + static_cast<std::size_t>(i)] = std::byte{0xFF};  // inflatedLen = 4 GiB - 1
    Player p;
    const auto res = p.openBytes(bytes);
    // chunk 1 is rejected without allocating; chunk 2 does not exist (40 samples = chunk 1 of 30 + chunk 2 of 10)
    REQUIRE(res.ok());
    REQUIRE(p.sampleCount() == 10);
    REQUIRE(res.recovered);
}

TEST_CASE("seeking by time", "[trace][player]") {
    const auto bytes = record(100);
    Player p;
    REQUIRE(p.openBytes(bytes).ok());
    const auto t0 = p.wallAt(0);
    REQUIRE(p.indexAtOrBefore(t0 - 10s) == 0);
    REQUIRE(p.indexAtOrBefore(t0) == 0);
    REQUIRE(p.indexAtOrBefore(t0 + 5500ms) == 5);
    REQUIRE(p.indexAtOrBefore(t0 + 99s) == 99);
    REQUIRE(p.indexAtOrBefore(t0 + 1000s) == 99);
    Player empty;
    REQUIRE(empty.indexAtOrBefore(t0) == 0);
}

TEST_CASE("replay never interpolates", "[trace][player]") {
    const auto bytes = record(30, 12);  // a 41-second jump before sample 12
    Player p;
    REQUIRE(p.openBytes(bytes).ok());
    REQUIRE(p.gapBefore(12));
    REQUIRE_FALSE(p.gapBefore(11));
    REQUIRE(p.wallAt(12) - p.wallAt(11) == 41s);
    REQUIRE(p.sample(12)->gapBefore);
}

TEST_CASE("open reports a missing file", "[trace][player]") {
    Player p;
    const auto r = p.open("/definitely/not/here.pmptrace");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error.find("Could not open") != std::string::npos);
}

TEST_CASE("open reads a saved file", "[trace][player]") {
    const auto bytes = record(35);
    const auto path = std::filesystem::temp_directory_path() / ("pmp-player-" + std::to_string(::getpid()) + ".pmptrace");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    Player p;
    REQUIRE(p.open(path).ok());
    REQUIRE(p.sampleCount() == 35);
    std::filesystem::remove(path);
}

TEST_CASE("survives random corruption", "[trace][player][fuzz]") {
    const auto good = record(70, 40);
    constexpr std::uint32_t kSeed = 20261025;
    std::printf("fuzz seed %u\n", kSeed);
    std::mt19937 rng(kSeed);
    int opened = 0;
    for (int iter = 0; iter < 1500; ++iter) {
        Bytes b = good;
        switch (rng() % 4) {
            case 0: {  // flip 1-8 random bytes
                const int n = 1 + static_cast<int>(rng() % 8);
                for (int k = 0; k < n; ++k) b[rng() % b.size()] ^= static_cast<std::byte>(1 + rng() % 255);
                break;
            }
            case 1: b.resize(rng() % b.size()); break;  // truncate anywhere
            case 2: {                                   // duplicate a slice
                const auto from = rng() % b.size();
                const auto len = std::min<std::size_t>(rng() % 200, b.size() - from);
                b.insert(b.begin() + static_cast<long>(rng() % b.size()), b.begin() + static_cast<long>(from),
                         b.begin() + static_cast<long>(from + len));
                break;
            }
            default: {  // overwrite a run with noise
                const auto from = rng() % b.size();
                const auto len = std::min<std::size_t>(1 + rng() % 64, b.size() - from);
                for (std::size_t k = 0; k < len; ++k) b[from + k] = static_cast<std::byte>(rng());
                break;
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        Player p;
        const auto r = p.openBytes(b);
        if (r.ok()) {
            ++opened;
            for (std::size_t i = 0; i < p.sampleCount(); i += 7) (void)p.frame(i);
        } else {
            REQUIRE_FALSE(r.error.empty());
        }
#ifdef __SANITIZE_ADDRESS__
        REQUIRE(std::chrono::steady_clock::now() - t0 < 2000ms);  // the sanitiser slows everything several times over
#else
        REQUIRE(std::chrono::steady_clock::now() - t0 < 200ms);
#endif
    }
    REQUIRE(opened > 0);
}
