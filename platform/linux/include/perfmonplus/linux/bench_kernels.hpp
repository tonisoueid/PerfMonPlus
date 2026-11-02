// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pmp::linux {

// Bumped whenever a kernel's work changes, so scores from different versions are never compared (BEN-04).
inline constexpr int kBenchVersion = 1;

// A build without optimisation runs these loops several times slower: its scores say nothing about the machine.
#ifdef NDEBUG
inline constexpr bool kOptimizedBuild = true;
#else
inline constexpr bool kOptimizedBuild = false;
#endif

enum class BenchId { CpuSingle, CpuMulti, MemBandwidth, MemLatency, DiskSeqWrite, DiskSeqRead, DiskRandRead };
inline constexpr int kBenchCount = 7;

std::string benchName(BenchId id);   // "CPU single-thread"
std::string benchKey(BenchId id);    // "cpu-single": stable, stored in results files

struct BenchParams {  // the production defaults; tests shrink them
    double seconds = 5;                          // CPU benchmarks run this long, doing fixed-size work units
    std::size_t memBytes = 256u << 20;           // bandwidth working set
    std::size_t latencyBytes = 64u << 20;        // pointer-chase working set
    std::uint64_t diskBytes = 1ull << 30;        // temp file size
    double randomSeconds = 3;                    // random-read test length
    std::filesystem::path tempDir;               // default: $XDG_CACHE_HOME/perfmonplus (never a RAM-backed /tmp)
    int threads = 0;                             // 0: one per logical CPU
};

std::filesystem::path defaultBenchDir();         // $XDG_CACHE_HOME/perfmonplus, else ~/.cache/perfmonplus

struct BenchResult {
    BenchId id = BenchId::CpuSingle;
    double raw = 0;                              // 0 when cancelled or failed
    std::string rawUnit;                         // "Mops/s", "GB/s", "ns", "MB/s", "IOPS"
    double score = 0;                            // raw against the reference table, 1000 = the reference machine
    double seconds = 0;                          // wall time the run took
    std::uint64_t checksum = 0;                  // proves the work was done and was correct
    bool cancelled = false;
    std::string note;                            // "data mismatch", "buffered I/O: this file system refuses O_DIRECT"…
};

class Benchmark {
public:
    explicit Benchmark(BenchParams params);
    ~Benchmark();
    Benchmark(const Benchmark&) = delete;
    Benchmark& operator=(const Benchmark&) = delete;

    // `progress` receives 0..1. Honours `cancel` within a few hundred milliseconds and removes the temp file then.
    BenchResult run(BenchId id, const std::atomic<bool>& cancel, const std::function<void(double)>& progress = {});
    void cleanup();  // removes this run's temp file now

    // "" when the test may start, else the reason it must not (too little free space or memory, unwritable folder).
    static std::string preflight(BenchId id, const BenchParams& params);
    static std::uint64_t estimatedSeconds(BenchId id, const BenchParams& params);
    static void removeStaleTempFiles(const std::filesystem::path& dir);  // pmp-bench-*.tmp left by a crash

    // Exposed for tests: the work itself, with a fixed amount of it.
    static std::uint64_t cpuChecksum(std::uint64_t units, std::uint64_t seed = 1);
    static std::uint64_t cpuMultiChecksum(int threads, std::uint64_t unitsEach);
    static std::vector<std::uint32_t> makeCycle(std::size_t n, std::uint64_t seed);  // one cycle through every slot

private:
    BenchResult cpuSingle(const std::atomic<bool>&, const std::function<void(double)>&);
    BenchResult cpuMulti(const std::atomic<bool>&, const std::function<void(double)>&);
    BenchResult memBandwidth(const std::atomic<bool>&, const std::function<void(double)>&);
    BenchResult memLatency(const std::atomic<bool>&, const std::function<void(double)>&);
    BenchResult diskWrite(const std::atomic<bool>&, const std::function<void(double)>&);
    BenchResult diskRead(const std::atomic<bool>&, const std::function<void(double)>&);
    BenchResult diskRandom(const std::atomic<bool>&, const std::function<void(double)>&);
    std::filesystem::path tempFile() const;

    BenchParams params_;
    std::filesystem::path file_;
    bool fileComplete_ = false;
    bool buffered_ = false;
};

double scoreFor(BenchId id, double raw);  // raw / reference * 1000 (reciprocal for latency, where smaller is better)
std::string rawUnitOf(BenchId id);

}  // namespace pmp::linux
