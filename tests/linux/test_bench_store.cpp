// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/bench_store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <unistd.h>

using namespace pmp::linux;
namespace fs = std::filesystem;

namespace {

fs::path tempFile() {
    static int n = 0;
    const auto d = fs::temp_directory_path() / ("pmp-benchstore-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
    fs::create_directories(d);
    return d / "benchmarks.json";
}

BenchRecord record(std::int64_t when) {
    BenchRecord r;
    r.appVersion = "0.1.0";
    r.cpuModel = "Test CPU";
    r.kernel = "7.0";
    r.logicalCpus = 8;
    r.memoryBytes = 16ull << 30;
    r.wallMs = when;
    BenchResult a;
    a.id = BenchId::CpuSingle;
    a.raw = 2600.5;
    a.rawUnit = "Mops/s";
    a.score = 981.3;
    a.seconds = 5.1;
    a.checksum = 18446744073709551615ULL;  // needs all 64 bits
    a.note = "";
    BenchResult b;
    b.id = BenchId::DiskSeqRead;
    b.cancelled = true;
    b.note = "cancelled";
    r.results = {a, b};
    return r;
}

}  // namespace

TEST_CASE("a missing file is empty without a warning", "[bench-store]") {
    BenchStore s(tempFile());
    std::string warning;
    REQUIRE(s.load(&warning).empty());
    REQUIRE(warning.empty());
}

TEST_CASE("records round-trip, including 64-bit checksums", "[bench-store]") {
    const auto f = tempFile();
    BenchStore s(f);
    std::string err;
    REQUIRE(s.append(record(1'700'000'000'000), &err));
    const auto back = s.load();
    REQUIRE(back.size() == 1);
    REQUIRE(back[0].cpuModel == "Test CPU");
    REQUIRE(back[0].logicalCpus == 8);
    REQUIRE(back[0].memoryBytes == (16ull << 30));
    REQUIRE(back[0].benchVersion == kBenchVersion);
    REQUIRE(back[0].results.size() == 2);
    REQUIRE(back[0].results[0].raw == 2600.5);
    REQUIRE(back[0].results[0].checksum == 18446744073709551615ULL);
    REQUIRE(back[0].results[1].cancelled);
    REQUIRE(back[0].results[1].id == BenchId::DiskSeqRead);
}

TEST_CASE("only the newest 200 are kept", "[bench-store]") {
    BenchStore s(tempFile());
    for (int i = 0; i < 205; ++i) REQUIRE(s.append(record(i)));
    const auto runs = s.load();
    REQUIRE(runs.size() == 200);
    REQUIRE(runs.front().wallMs == 5);
    REQUIRE(runs.back().wallMs == 204);
}

TEST_CASE("a corrupt file is moved aside and a warning explains", "[bench-store]") {
    const auto f = tempFile();
    std::ofstream(f) << "{ this is not json";
    BenchStore s(f);
    std::string warning;
    REQUIRE(s.load(&warning).empty());
    REQUIRE(warning.find("was damaged") != std::string::npos);
    REQUIRE(warning.find(".bad") != std::string::npos);
    REQUIRE(fs::exists(fs::path(f.string() + ".bad")));
    REQUIRE_FALSE(fs::exists(f));
    REQUIRE(s.append(record(1)));  // and a fresh history starts
    REQUIRE(s.load().size() == 1);
}

TEST_CASE("valid json of the wrong shape is also set aside", "[bench-store]") {
    const auto f = tempFile();
    std::ofstream(f) << R"({"runs": 5})";
    std::string warning;
    REQUIRE(BenchStore(f).load(&warning).empty());
    REQUIRE_FALSE(warning.empty());
}

TEST_CASE("a test this build does not know is ignored, not an error", "[bench-store]") {
    const auto f = tempFile();
    std::ofstream(f) << R"({"version":1,"runs":[{"benchVersion":1,"cpuModel":"X","logicalCpus":4,"wallMs":5,"results":[{"id":"gpu-compute","raw":5},{"id":"cpu-single","raw":7,"score":3,"checksum":"12"}]}]})";
    std::string warning;
    const auto runs = BenchStore(f).load(&warning);
    REQUIRE(warning.empty());
    REQUIRE(runs.size() == 1);
    REQUIRE(runs[0].results.size() == 1);
    REQUIRE(runs[0].results[0].id == BenchId::CpuSingle);
}

TEST_CASE("saving into a missing folder creates it; an unwritable one reports", "[bench-store]") {
    const auto f = tempFile().parent_path() / "deeper/still/benchmarks.json";
    REQUIRE(BenchStore(f).append(record(1)));
    REQUIRE(fs::exists(f));
    std::string err;
    REQUIRE_FALSE(BenchStore("/proc/no/such/benchmarks.json").append(record(1), &err));
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("the optimised-build flag is stored, and old files count as optimised", "[bench-store][minor]") {
    const auto f = tempFile();
    BenchStore s(f);
    auto r = record(1);
    r.optimized = false;
    REQUIRE(s.append(r));
    REQUIRE_FALSE(s.load()[0].optimized);
    const auto old = tempFile();
    std::ofstream(old) << R"({"version":1,"runs":[{"benchVersion":1,"cpuModel":"X","logicalCpus":4,"wallMs":5,"results":[]}]})";
    REQUIRE(BenchStore(old).load()[0].optimized);
}
