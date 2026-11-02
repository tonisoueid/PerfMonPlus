// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/bench_kernels.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <set>
#include <thread>

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

using namespace pmp::linux;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

class Dir {
public:
    Dir() {
        static int n = 0;
        dir_ = fs::temp_directory_path() / ("pmp-bench-test-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
        fs::create_directories(dir_);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

private:
    fs::path dir_;
};

BenchParams small(const fs::path& dir) {
    BenchParams p;
    p.seconds = 0.2;
    p.memBytes = 8u << 20;
    p.latencyBytes = 4u << 20;
    p.diskBytes = 8u << 20;
    p.randomSeconds = 0.2;
    p.tempDir = dir;
    p.threads = 2;
    return p;
}

std::atomic<bool> never{false};

std::size_t tempFiles(const fs::path& dir) {
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir)) n += e.path().filename().string().starts_with("pmp-bench-") ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("cpu single-thread checksum is deterministic", "[bench]") {
    // The work is fixed arithmetic: this number changes only with kBenchVersion.
    REQUIRE(Benchmark::cpuChecksum(1000) == 6222271340368864329ULL);
    REQUIRE(Benchmark::cpuChecksum(1000) == Benchmark::cpuChecksum(1000));
    REQUIRE(Benchmark::cpuChecksum(1000) != Benchmark::cpuChecksum(1001));
    Dir d;
    Benchmark b(small(d.path()));
    const auto r = b.run(BenchId::CpuSingle, never);
    REQUIRE(r.raw > 0);
    REQUIRE(r.rawUnit == "Mops/s");
    REQUIRE(r.checksum != 0);
    REQUIRE(r.score > 0);
    REQUIRE_FALSE(r.cancelled);
}

TEST_CASE("cpu multi-thread uses every logical cpu", "[bench]") {
    REQUIRE(Benchmark::cpuMultiChecksum(4, 200) == 4743987502846236243ULL);
    Dir d;
    auto p = small(d.path());
    p.threads = 0;  // one per logical CPU
    Benchmark b(p);
    const auto r = b.run(BenchId::CpuMulti, never);
    REQUIRE(r.note == std::to_string(std::max(1u, std::thread::hardware_concurrency())) + " threads");
    REQUIRE(r.raw > 0);
}

TEST_CASE("memory bandwidth reports GB/s and verifies the triad", "[bench]") {
    Dir d;
    Benchmark b(small(d.path()));
    const auto r = b.run(BenchId::MemBandwidth, never);
    REQUIRE(r.rawUnit == "GB/s");
    REQUIRE(r.raw > 0.1);
    REQUIRE(r.note.empty());  // no "data mismatch"
}

TEST_CASE("latency reports ns and the chase visits every slot once", "[bench]") {
    const auto cycle = Benchmark::makeCycle(1000, 42);
    std::set<std::uint32_t> seen;
    std::uint32_t at = 0;
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(seen.insert(at).second);  // never revisits a slot before the whole cycle is done
        at = cycle[at];
    }
    REQUIRE(at == 0);  // and it is a single cycle: back at the start after n steps
    REQUIRE(seen.size() == 1000);
    Dir d;
    Benchmark b(small(d.path()));
    const auto r = b.run(BenchId::MemLatency, never);
    REQUIRE(r.rawUnit == "ns");
    REQUIRE(r.raw > 0.1);
    REQUIRE(r.raw < 5000);
}

TEST_CASE("disk write then read round-trips a pattern and verifies it", "[bench]") {
    Dir d;
    Benchmark b(small(d.path()));
    const auto w = b.run(BenchId::DiskSeqWrite, never);
    REQUIRE(w.raw > 0);
    REQUIRE(w.rawUnit == "MB/s");
    REQUIRE(tempFiles(d.path()) == 1);  // kept for the read tests
    const auto r = b.run(BenchId::DiskSeqRead, never);
    REQUIRE(r.raw > 0);
    REQUIRE(r.note.find("mismatch") == std::string::npos);
    const auto q = b.run(BenchId::DiskRandRead, never);
    REQUIRE(q.raw > 0);
    REQUIRE(q.rawUnit == "IOPS");
    b.cleanup();
    REQUIRE(tempFiles(d.path()) == 0);
}

TEST_CASE("a corrupted file is a failed test, never a fast fake score", "[bench]") {
    Dir d;
    Benchmark b(small(d.path()));
    REQUIRE(b.run(BenchId::DiskSeqWrite, never).raw > 0);
    for (const auto& e : fs::directory_iterator(d.path())) {
        const int fd = ::open(e.path().c_str(), O_WRONLY);
        REQUIRE(fd >= 0);
        const char junk[8] = {'j', 'u', 'n', 'k', 'j', 'u', 'n', 'k'};
        REQUIRE(::pwrite(fd, junk, 8, 3 * 1024 * 1024 + 8) == 8);  // inside the 4th megabyte
        ::close(fd);
    }
    const auto r = b.run(BenchId::DiskSeqRead, never);
    REQUIRE(r.note == "data mismatch");
    REQUIRE(r.raw == 0);
    REQUIRE(r.score == 0);
}

TEST_CASE("refuses when free space is too low", "[bench]") {
    Dir d;
    auto p = small(d.path());
    struct statvfs sv{};
    REQUIRE(::statvfs(d.path().c_str(), &sv) == 0);
    p.diskBytes = static_cast<std::uint64_t>(sv.f_bavail) * sv.f_frsize + (10ull << 30);
    const auto why = Benchmark::preflight(BenchId::DiskSeqWrite, p);
    REQUIRE(why.starts_with("Needs "));
    REQUIRE(why.find(" free in " + d.path().string()) != std::string::npos);
    REQUIRE(why.find("; only ") != std::string::npos);
    REQUIRE(why.ends_with(" is available."));
    REQUIRE(Benchmark::preflight(BenchId::CpuSingle, p).empty());  // CPU tests need no disk
    REQUIRE(Benchmark::preflight(BenchId::DiskSeqWrite, small(d.path())).empty());
}

TEST_CASE("refuses an unwritable folder", "[bench]") {
    if (::geteuid() == 0) SKIP("root can write anywhere");
    BenchParams p;
    p.tempDir = "/proc/pmp-no-such";
    REQUIRE_FALSE(Benchmark::preflight(BenchId::DiskSeqWrite, p).empty());
}

TEST_CASE("cancel removes the temp file and returns promptly", "[bench]") {
    Dir d;
    auto p = small(d.path());
    p.diskBytes = 512u << 20;
    Benchmark b(p);
    std::atomic<bool> cancel{false};
    const auto start = std::chrono::steady_clock::now();
    const auto r = b.run(BenchId::DiskSeqWrite, cancel, [&](double f) {
        if (f > 0.0) cancel = true;  // cancelled after the first progress report
    });
    REQUIRE(r.cancelled);
    REQUIRE(r.raw == 0);
    REQUIRE(r.score == 0);
    REQUIRE(tempFiles(d.path()) == 0);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
}

TEST_CASE("destroying a benchmark removes its temp file", "[bench]") {
    Dir d;
    {
        Benchmark b(small(d.path()));
        b.run(BenchId::DiskSeqWrite, never);
        REQUIRE(tempFiles(d.path()) == 1);
    }
    REQUIRE(tempFiles(d.path()) == 0);
}

TEST_CASE("stale temp files are removed at start", "[bench]") {
    Dir d;
    std::ofstream(d.path() / "pmp-bench-4194301.tmp") << "x";
    std::ofstream(d.path() / "pmp-bench-4194302.tmp") << "x";
    std::ofstream(d.path() / "keep.txt") << "x";
    std::ofstream(d.path() / "pmp-bench-notes.txt") << "x";
    Benchmark::removeStaleTempFiles(d.path());
    REQUIRE_FALSE(fs::exists(d.path() / "pmp-bench-4194301.tmp"));
    REQUIRE_FALSE(fs::exists(d.path() / "pmp-bench-4194302.tmp"));
    REQUIRE(fs::exists(d.path() / "keep.txt"));
    REQUIRE(fs::exists(d.path() / "pmp-bench-notes.txt"));
    Benchmark::removeStaleTempFiles(d.path() / "missing");  // a missing folder is fine
}

TEST_CASE("the default temp folder is under the cache, never /tmp", "[bench]") {
    const char* oldHome = std::getenv("HOME");
    const char* oldXdg = std::getenv("XDG_CACHE_HOME");
    const std::string home = oldHome ? oldHome : "", xdg = oldXdg ? oldXdg : "";
    setenv("XDG_CACHE_HOME", "/tmp/pmp-xdg-cache", 1);
    REQUIRE(defaultBenchDir() == "/tmp/pmp-xdg-cache/perfmonplus");
    unsetenv("XDG_CACHE_HOME");
    setenv("HOME", "/home/someone", 1);
    REQUIRE(defaultBenchDir() == "/home/someone/.cache/perfmonplus");
    // put the environment back: other tests in this process read it
    if (oldHome) setenv("HOME", home.c_str(), 1);
    else unsetenv("HOME");
    if (oldXdg) setenv("XDG_CACHE_HOME", xdg.c_str(), 1);
}

TEST_CASE("scores are relative to the reference machine", "[bench]") {
    REQUIRE(scoreFor(BenchId::CpuSingle, 2650) == Approx(1000));
    REQUIRE(scoreFor(BenchId::CpuSingle, 5300) == Approx(2000));
    REQUIRE(scoreFor(BenchId::MemLatency, 118) == Approx(1000));
    REQUIRE(scoreFor(BenchId::MemLatency, 59) == Approx(2000));  // half the latency is twice as good
    REQUIRE(scoreFor(BenchId::DiskRandRead, 0) == 0);
    REQUIRE(benchKey(BenchId::DiskSeqWrite) == "disk-seq-write");
    REQUIRE(benchName(BenchId::CpuMulti) == "CPU multi-thread");
    REQUIRE(kBenchVersion == 1);
}

TEST_CASE("estimates are sensible", "[bench]") {
    BenchParams p;
    REQUIRE(Benchmark::estimatedSeconds(BenchId::CpuSingle, p) == 6);
    REQUIRE(Benchmark::estimatedSeconds(BenchId::DiskSeqWrite, p) > 5);
    REQUIRE(Benchmark::estimatedSeconds(BenchId::DiskRandRead, p) == 4);
}

TEST_CASE("stale temp files of a running instance are left alone", "[bench][minor]") {
    Dir d;
    const auto mine = "pmp-bench-" + std::to_string(::getpid()) + ".tmp";
    std::ofstream(d.path() / mine) << "x";                  // this very process: alive
    std::ofstream(d.path() / "pmp-bench-1.tmp") << "x";     // pid 1 (init) is alive: another instance's file
    std::ofstream(d.path() / "pmp-bench-4194300.tmp") << "x";  // no such process: a crash left it
    Benchmark::removeStaleTempFiles(d.path());
    REQUIRE(fs::exists(d.path() / mine));
    REQUIRE(fs::exists(d.path() / "pmp-bench-1.tmp"));
    REQUIRE_FALSE(fs::exists(d.path() / "pmp-bench-4194300.tmp"));
}
