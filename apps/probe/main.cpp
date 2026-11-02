// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include "frame_json.hpp"

#include <perfmonplus/core/gap.hpp>
#include <perfmonplus/core/sampler.hpp>
#include <perfmonplus/core/version.hpp>
#include <perfmonplus/linux/cpu_provider.hpp>
#include <perfmonplus/linux/disk_provider.hpp>
#include <perfmonplus/linux/memory_provider.hpp>
#include <perfmonplus/linux/net_provider.hpp>
#include <perfmonplus/linux/process_provider.hpp>

#include <time.h>
#include <unistd.h>

#include <charconv>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct Options {
    long intervalMs = 1000;
    long count = 0;  // 0 = forever
    long top = 12;
    bool includeSelf = false;
    std::string procRoot, sysRoot;
};

void usage() {
    std::fputs(
        "usage: perfmonplus-probe [--interval-ms N] [--count N] [--top N] [--include-self]\n"
        "                         [--proc-root DIR] [--sys-root DIR] [--version]\n"
        "Prints one JSON object per sample (JSON Lines) on stdout.\n",
        stderr);
}

bool parseLong(const char* s, long min, long& out) {
    const std::string_view v(s);
    long x = 0;
    const auto r = std::from_chars(v.data(), v.data() + v.size(), x);
    if (r.ec != std::errc{} || r.ptr != v.data() + v.size() || x < min) return false;
    out = x;
    return true;
}

std::chrono::nanoseconds bootTime() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--version") {
            std::printf("perfmonplus-probe %s\n", std::string(pmp::versionString()).c_str());
            return 0;
        } else if (a == "--include-self") {
            o.includeSelf = true;
        } else if (a == "--interval-ms" || a == "--count" || a == "--top") {
            const char* v = next();
            long& target = a == "--interval-ms" ? o.intervalMs : a == "--count" ? o.count : o.top;
            if (!v || !parseLong(v, a == "--interval-ms" ? 1 : 0, target) || (a == "--interval-ms" && target > 3'600'000)) {
                usage();
                return 2;
            }
        } else if (a == "--proc-root" || a == "--sys-root") {
            const char* v = next();
            if (!v || !*v) {  // an empty root would silently mean "the real /proc"
                usage();
                return 2;
            }
            (a == "--proc-root" ? o.procRoot : o.sysRoot) = v;
        } else {
            usage();
            return 2;
        }
    }

    auto root = pmp::linux::ProcRoot::live();
    if (!o.procRoot.empty()) root.proc = o.procRoot;
    if (!o.sysRoot.empty()) root.sys = o.sysRoot;

    pmp::linux::ProcessOptions po;
    po.selfPid = getpid();
    po.includeSelf = o.includeSelf;

    std::vector<std::unique_ptr<pmp::Provider>> providers;
    providers.push_back(std::make_unique<pmp::linux::CpuProvider>(root));
    providers.push_back(std::make_unique<pmp::linux::MemoryProvider>(root));
    providers.push_back(std::make_unique<pmp::linux::DiskProvider>(root));
    providers.push_back(std::make_unique<pmp::linux::NetProvider>(root));
    providers.push_back(std::make_unique<pmp::linux::ProcessProvider>(root, std::move(po)));
    pmp::Sampler sampler(std::move(providers));

    const std::chrono::milliseconds interval(o.intervalMs);
    auto next = std::chrono::steady_clock::now();
    pmp::MonoTime prevMono{};
    std::chrono::nanoseconds prevBoot{};

    for (long n = 0; o.count == 0 || n < o.count; ++n) {
        const auto mono = std::chrono::steady_clock::now();
        const auto boot = bootTime();
        const bool gap = n > 0 && pmp::shouldInsertGap(interval, mono - prevMono, boot - prevBoot);
        const auto frame = sampler.sample(mono, boot, std::chrono::system_clock::now());
        std::cout << pmp::probe::dumpLine(pmp::probe::toJson(frame, static_cast<std::size_t>(o.top), gap)) << std::endl;
        prevMono = mono;
        prevBoot = boot;

        next += interval;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;  // don't burst to catch up after a stall or suspend
        std::this_thread::sleep_until(next);
    }
    return 0;
}
