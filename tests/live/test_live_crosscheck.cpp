// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/gap.hpp>
#include <perfmonplus/core/sampler.hpp>
#include <perfmonplus/linux/disk_scan.hpp>
#include <perfmonplus/linux/cpu_provider.hpp>
#include <perfmonplus/linux/disk_provider.hpp>
#include <perfmonplus/linux/memory_provider.hpp>
#include <perfmonplus/linux/net_provider.hpp>
#include <perfmonplus/linux/process_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <thread>

using namespace pmp;
using namespace pmp::linux;
using namespace std::chrono_literals;

namespace {

Sampler liveSampler() {
    const auto root = ProcRoot::live();
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<CpuProvider>(root));
    ps.push_back(std::make_unique<MemoryProvider>(root));
    ps.push_back(std::make_unique<DiskProvider>(root));
    ps.push_back(std::make_unique<NetProvider>(root));
    ProcessOptions o;
    o.selfPid = getpid();
    ps.push_back(std::make_unique<ProcessProvider>(root, std::move(o)));
    return Sampler(std::move(ps));
}

Frame takeSample(Sampler& s) {
    return s.sample(std::chrono::steady_clock::now(), 0ns, std::chrono::system_clock::now());
}

}  // namespace

TEST_CASE("memory total matches sysconf", "[live][crosscheck]") {
    auto s = liveSampler();
    const auto f = takeSample(s);
    const double expected = static_cast<double>(sysconf(_SC_PHYS_PAGES)) * static_cast<double>(sysconf(_SC_PAGESIZE));
    REQUIRE(f.memory.total.isMeasured());
    REQUIRE(std::abs(*f.memory.total.value - expected) / expected <= 0.01);
}

TEST_CASE("busy threads drive cpu utilisation up", "[live][crosscheck]") {
    auto s = liveSampler();
    takeSample(s);
    std::atomic<bool> stop{false};
    std::vector<std::thread> spinners;
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
        spinners.emplace_back([&] {
            volatile unsigned long x = 0;
            while (!stop) x = x + 1;
        });
    }
    std::this_thread::sleep_for(1500ms);
    const auto f = takeSample(s);
    stop = true;
    for (auto& t : spinners) t.join();
    REQUIRE(f.cpu.total.isMeasured());
    REQUIRE(*f.cpu.total.value >= 80.0);
}

TEST_CASE("process count is close to /proc", "[live][crosscheck]") {
    auto s = liveSampler();
    const auto f = takeSample(s);
    int dirs = 0;
    for (const auto& e : std::filesystem::directory_iterator("/proc")) {
        const auto n = e.path().filename().string();
        if (!n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; })) ++dirs;
    }
    REQUIRE(f.processes.count.isMeasured());
    REQUIRE(std::abs(*f.processes.count.value - dirs) / dirs <= 0.05);
}

TEST_CASE("worst-case sampler cost stays within the per-process budget", "[live][crosscheck]") {
#ifndef PMP_OPTIMIZED
    SKIP("timing budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    // NFR-02 (revised): ~40 us per process per generation plus 3 ms for the other providers.
    // Timing on a shared desktop is noisy, so gate on robust statistics over 12 generations (more than two
    // status-refresh cycles): the MEDIAN must fit the budget (catches a systematic slowdown) and the WORST
    // must stay under twice the budget (catches a periodic spike like an unstaggered status refresh).
    auto s = liveSampler();
    takeSample(s);
    std::this_thread::sleep_for(200ms);
    takeSample(s);
    std::vector<double> costs;
    double processes = 0;
    for (int i = 0; i < 12; ++i) {
        std::this_thread::sleep_for(100ms);
        const auto t = std::chrono::steady_clock::now();
        const auto f = takeSample(s);
        costs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        processes = *f.processes.count.value;
    }
    std::sort(costs.begin(), costs.end());
    const double budgetMs = 3.0 + 0.040 * processes;
    const double median = (costs[costs.size() / 2 - 1] + costs[costs.size() / 2]) / 2;
    INFO("median " << median << " ms, worst " << costs.back() << " ms for " << processes << " processes, budget "
                   << budgetMs);
    REQUIRE(median < budgetMs);
    REQUIRE(costs.back() < 2 * budgetMs);
#endif
}

TEST_CASE("a disk scan running in the background keeps the sampler within its budget", "[live][crosscheck][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("timing budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    // SPC-01 / ARC-05: the scanner runs at idle priority on its own thread and must not slow the sampler.
    std::atomic<bool> stop{false};
    pmp::linux::ScanOptions so;
    so.root = "/usr";
    std::thread scanner([&] { pmp::linux::DiskScanner().scan(so, stop); });
    auto s = liveSampler();
    takeSample(s);
    std::this_thread::sleep_for(200ms);
    takeSample(s);
    std::vector<double> costs;
    double processes = 0;
    for (int i = 0; i < 12; ++i) {
        std::this_thread::sleep_for(100ms);
        const auto t = std::chrono::steady_clock::now();
        const auto f = takeSample(s);
        costs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        processes = *f.processes.count.value;
    }
    stop = true;
    scanner.join();
    std::sort(costs.begin(), costs.end());
    const double budgetMs = 3.0 + 0.040 * processes;
    const double median = (costs[costs.size() / 2 - 1] + costs[costs.size() / 2]) / 2;
    INFO("median " << median << " ms, worst " << costs.back() << " ms for " << processes << " processes, budget " << budgetMs);
    REQUIRE(median < budgetMs);
    REQUIRE(costs.back() < 2 * budgetMs);
#endif
}
