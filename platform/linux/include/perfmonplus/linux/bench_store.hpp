// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/bench_kernels.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace pmp::linux {

// One full or partial run: the machine it ran on, the version of the tests, and each result (BEN-02).
struct BenchRecord {
    int benchVersion = kBenchVersion;
    bool optimized = kOptimizedBuild;  // scores of a debug build are only comparable with other debug-build runs
    std::string appVersion, cpuModel, kernel;  // never a host name or serial number
    int logicalCpus = 0;
    std::uint64_t memoryBytes = 0;
    std::int64_t wallMs = 0;  // when the run finished (milliseconds since the epoch)
    std::vector<BenchResult> results;
};

// Results stay on this machine (BEN-04: no leaderboard). A damaged file is set aside and replaced, never trusted.
class BenchStore {
public:
    explicit BenchStore(std::filesystem::path file) : file_(std::move(file)) {}
    static std::filesystem::path defaultFile();  // $XDG_DATA_HOME/perfmonplus/benchmarks.json

    std::vector<BenchRecord> load(std::string* warning = nullptr) const;
    bool append(const BenchRecord& record, std::string* error = nullptr) const;  // keeps the newest kKeep records

    static constexpr std::size_t kKeep = 200;

private:
    std::filesystem::path file_;
};

}  // namespace pmp::linux
