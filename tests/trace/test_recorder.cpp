// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/recorder.hpp>

#include "trace_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <unistd.h>

using namespace pmp;
using namespace pmp::trace;
using namespace pmp::testing;
using namespace std::chrono_literals;

namespace {

Recorder makeRecorder(RecorderOptions o = {}) {
    HostMeta h;
    h.os = "Linux test";
    h.logicalCpus = 2;
    return Recorder(h, 1000ms, o);
}

std::filesystem::path tempDir(const char* tag) {
    auto d = std::filesystem::temp_directory_path() / (std::string("pmp-rec-") + tag + "-" + std::to_string(::getpid()));
    std::filesystem::create_directories(d);
    return d;
}

}  // namespace

TEST_CASE("a recording round-trips through serialise", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 100; ++i) rec.append(noisyFrame(i), false);
    rec.stop();
    const auto bytes = rec.serialise();
    REQUIRE(std::memcmp(bytes.data(), "PMPTRACE", 8) == 0);
    REQUIRE(std::memcmp(bytes.data() + bytes.size() - 8, "PMPEND01", 8) == 0);
    const auto d = decodeAll(bytes);
    REQUIRE(d.ok);
    REQUIRE(d.info.format == 1);
    REQUIRE(d.info.host.os == "Linux test");
    REQUIRE(d.samples.size() == 100);
    for (int i : {0, 41, 99}) {
        const auto back = unflatten(d.samples[static_cast<std::size_t>(i)], 1);
        std::string why;
        INFO("sample " << i << ": " << why);
        const auto want = unflatten(flatten(noisyFrame(i), false, 20), 1);
        REQUIRE(sameRecorded(want, back, &why));
    }
}

TEST_CASE("size stays within the budget", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 60; ++i) rec.append(noisyFrame(i), false);
    rec.stop();
    const auto real = rec.serialise().size();
    std::printf("one minute, 8 cpus' worth of series, 20+ top processes: %zu bytes\n", real);
    REQUIRE(rec.compressedBytes() < 1'000'000);
    const auto est = static_cast<double>(rec.estimatedBytesPerMinute());
    REQUIRE(est > static_cast<double>(real) * 0.7);
    REQUIRE(est < static_cast<double>(real) * 1.3);
}

TEST_CASE("a gap is recorded as a gap", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 10; ++i) rec.append(noisyFrame(i), false);
    for (int i = 0; i < 10; ++i) rec.append(noisyFrame(60 + i), i == 0);  // a minute of sleep in between
    rec.stop();
    const auto d = decodeAll(rec.serialise());
    REQUIRE(d.ok);
    REQUIRE(d.samples.size() == 20);  // nothing invented across the gap
    REQUIRE(d.samples[10].gapBefore);
    REQUIRE_FALSE(d.samples[9].gapBefore);
    REQUIRE_FALSE(d.samples[11].gapBefore);
    REQUIRE(d.samples[10].wall - d.samples[9].wall == 51s);
}

TEST_CASE("append is ignored unless recording", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.append(noisyFrame(0), false);
    REQUIRE(rec.sampleCount() == 0);
    rec.start();
    rec.append(noisyFrame(1), false);
    rec.stop();
    rec.append(noisyFrame(2), false);
    REQUIRE(rec.sampleCount() == 1);
    REQUIRE(rec.elapsed() == 0ms);
}

TEST_CASE("elapsed comes from the recorded wall times", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 45; ++i) rec.append(noisyFrame(i), false);
    REQUIRE(rec.elapsed() == 44s);
}

TEST_CASE("ring mode keeps only the last N minutes", "[trace][recorder]") {
    RecorderOptions o;
    o.ring = 1min;
    auto rec = makeRecorder(o);
    rec.start();
    std::size_t bytesAt200 = 0;
    for (int i = 0; i < 300; ++i) {
        rec.append(noisyFrame(i), false);
        if (i == 200) bytesAt200 = rec.compressedBytes();
    }
    REQUIRE(rec.sampleCount() <= 60 + 30 + 30);
    REQUIRE(rec.sampleCount() >= 60);
    const auto d = decodeAll(rec.serialise());
    REQUIRE(d.ok);
    const auto newest = d.samples.back().wall;
    const auto oldest = d.samples.front().wall;
    REQUIRE(newest - oldest >= 59s);
    REQUIRE(newest - oldest <= 120s);
    REQUIRE(rec.compressedBytes() < bytesAt200 * 3 / 2);  // memory stopped growing
}

TEST_CASE("max length stops the recording", "[trace][recorder]") {
    RecorderOptions o;
    o.maxLength = 1min;
    auto rec = makeRecorder(o);
    rec.start();
    for (int i = 0; i < 120; ++i) rec.append(noisyFrame(i), false);
    REQUIRE(rec.state() == Recorder::State::Full);
    REQUIRE(rec.sampleCount() == 60);
    rec.append(noisyFrame(200), false);
    REQUIRE(rec.sampleCount() == 60);
    rec.start();  // a full recorder does not restart by itself
    REQUIRE(rec.state() == Recorder::State::Full);
}

TEST_CASE("save writes atomically", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 40; ++i) rec.append(noisyFrame(i), false);
    rec.stop();
    const auto dir = tempDir("save");
    const auto path = dir / "a.pmptrace";
    std::string err;
    REQUIRE(rec.save(path, &err));
    REQUIRE(std::filesystem::exists(path));
    REQUIRE_FALSE(std::filesystem::exists(dir / "a.pmptrace.partial"));
    std::ifstream in(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(raw.size() == rec.serialise().size());

    std::string err2;
    REQUIRE_FALSE(rec.save(dir / "no-such-dir" / "b.pmptrace", &err2));
    REQUIRE_FALSE(err2.empty());
    REQUIRE_FALSE(std::filesystem::exists(dir / "no-such-dir"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("saving nothing is refused with a reason", "[trace][recorder]") {
    auto rec = makeRecorder();
    std::string err;
    REQUIRE_FALSE(rec.save("/tmp/never.pmptrace", &err));
    REQUIRE(err == "There is nothing recorded to save.");
}

TEST_CASE("save of the last 5 minutes", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 600; ++i) rec.append(noisyFrame(i), false);
    rec.stop();
    const auto d = decodeAll(rec.serialise(5min));
    REQUIRE(d.ok);
    REQUIRE(d.samples.size() >= 300);       // never fewer than asked
    REQUIRE(d.samples.size() <= 300 + 30);  // rounded up to a chunk
    REQUIRE(d.samples.back().wall == decodeAll(rec.serialise()).samples.back().wall);
}

TEST_CASE("identical input gives identical bytes", "[trace][recorder]") {
    RecorderOptions o;
    o.app = "PerfMonPlus test";
    auto make = [&] {
        auto rec = makeRecorder(o);
        rec.start();
        for (int i = 0; i < 70; ++i) rec.append(noisyFrame(i, 5), i == 33);
        rec.stop();
        return rec.serialise();
    };
    REQUIRE(make() == make());
}

TEST_CASE("a trace holds no personal data", "[trace][recorder]") {
    auto rec = makeRecorder();
    rec.start();
    for (int i = 0; i < 40; ++i) rec.append(noisyFrame(i), false);
    rec.stop();
    const auto bytes = rec.serialise();
    // Compressed bytes would hide strings, so check what goes into the chunks instead.
    const auto d = decodeAll(bytes);
    REQUIRE(d.ok);
    for (const auto& s : d.samples) {
        for (const auto& t : s.texts) {
            REQUIRE(t.value.find("alice") == std::string::npos);
            REQUIRE(t.value.find("/home") == std::string::npos);
        }
        for (const auto& p : s.procs) REQUIRE(p.name.find("alice") == std::string::npos);
    }
    const std::string whole(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    REQUIRE(whole.find("alice") == std::string::npos);
    char host[256] = {};
    ::gethostname(host, sizeof host - 1);
    if (host[0]) REQUIRE(whole.find(host) == std::string::npos);
}
