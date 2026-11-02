// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/bench_kernels.hpp>

#include <perfmonplus/linux/proc_fs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <random>
#include <thread>

#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace pmp::linux {
namespace {

namespace fs = std::filesystem;
using clock = std::chrono::steady_clock;

double secondsSince(clock::time_point t) { return std::chrono::duration<double>(clock::now() - t).count(); }

// Reference machine: the development laptop (8 logical CPUs, NVMe SSD), measured with a Release build on 2026-10-25.
// A score of 1000 means "as fast as that machine"; the figures are rounded measurements and change only together
// with kBenchVersion.
struct Reference {
    BenchId id;
    double raw;
    bool smallerIsBetter;
};
constexpr Reference kReference[] = {
    {BenchId::CpuSingle, 2650.0, false},    // Mops/s
    {BenchId::CpuMulti, 14000.0, false},    // Mops/s
    {BenchId::MemBandwidth, 24.0, false},   // GB/s
    {BenchId::MemLatency, 118.0, true},     // ns
    {BenchId::DiskSeqWrite, 1500.0, false}, // MB/s
    {BenchId::DiskSeqRead, 1300.0, false},  // MB/s
    {BenchId::DiskRandRead, 18500.0, false},// IOPS
};

constexpr std::size_t kUnitIterations = 4096;
constexpr std::size_t kChunk = 1u << 20;
constexpr std::size_t kBlock = 4096;

std::uint64_t xorshiftStar(std::uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 0x2545F4914F6CDD1DULL;
}

// One unit of CPU work: an integer mixing loop and a small floating-point matrix product, folded into a checksum.
std::uint64_t cpuUnit(std::uint64_t& state) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kUnitIterations; ++i) sum += xorshiftStar(state) >> 7;
    double a[8][8], b[8][8], c[8][8] = {};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            a[i][j] = static_cast<double>((state >> (i + j)) & 0xFF) / 256.0;
            b[i][j] = static_cast<double>((state >> (i * j % 13)) & 0xFF) / 256.0;
        }
    }
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 8; ++k) c[i][j] += a[i][k] * b[k][j];
    std::uint64_t f = 0;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) f += static_cast<std::uint64_t>(c[i][j] * 1e6);
    return sum ^ (f * 0x9E3779B97F4A7C15ULL);
}

// The work in a unit is ~ kUnitIterations * 6 integer ops + 8*8*8*2 FP ops: ~28,000 "ops"; throughput in Mops/s.
constexpr double kOpsPerUnit = static_cast<double>(kUnitIterations) * 6.0 + 1024.0;

struct AlignedBuf {
    void* p = nullptr;
    explicit AlignedBuf(std::size_t n) {
        if (posix_memalign(&p, 4096, n) != 0) p = nullptr;
    }
    ~AlignedBuf() { std::free(p); }
    AlignedBuf(const AlignedBuf&) = delete;
    AlignedBuf& operator=(const AlignedBuf&) = delete;
};

}  // namespace

std::string benchName(BenchId id) {
    switch (id) {
        case BenchId::CpuSingle: return "CPU single-thread";
        case BenchId::CpuMulti: return "CPU multi-thread";
        case BenchId::MemBandwidth: return "Memory bandwidth";
        case BenchId::MemLatency: return "Memory latency";
        case BenchId::DiskSeqWrite: return "Disk sequential write";
        case BenchId::DiskSeqRead: return "Disk sequential read";
        case BenchId::DiskRandRead: return "Disk random read";
    }
    return "?";
}

std::string benchKey(BenchId id) {
    switch (id) {
        case BenchId::CpuSingle: return "cpu-single";
        case BenchId::CpuMulti: return "cpu-multi";
        case BenchId::MemBandwidth: return "mem-bandwidth";
        case BenchId::MemLatency: return "mem-latency";
        case BenchId::DiskSeqWrite: return "disk-seq-write";
        case BenchId::DiskSeqRead: return "disk-seq-read";
        case BenchId::DiskRandRead: return "disk-rand-read";
    }
    return "?";
}

std::string rawUnitOf(BenchId id) {
    switch (id) {
        case BenchId::CpuSingle:
        case BenchId::CpuMulti: return "Mops/s";
        case BenchId::MemBandwidth: return "GB/s";
        case BenchId::MemLatency: return "ns";
        case BenchId::DiskSeqWrite:
        case BenchId::DiskSeqRead: return "MB/s";
        case BenchId::DiskRandRead: return "IOPS";
    }
    return {};
}

double scoreFor(BenchId id, double raw) {
    if (raw <= 0) return 0;
    for (const auto& r : kReference) {
        if (r.id == id) return r.smallerIsBetter ? r.raw / raw * 1000.0 : raw / r.raw * 1000.0;
    }
    return 0;
}

fs::path defaultBenchDir() {
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) return fs::path(x) / "perfmonplus";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".cache/perfmonplus";
    return fs::path(".cache/perfmonplus");
}

std::uint64_t Benchmark::cpuChecksum(std::uint64_t units, std::uint64_t seed) {
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ULL | 1, acc = 0;
    for (std::uint64_t u = 0; u < units; ++u) acc ^= cpuUnit(state) + u;
    return acc;
}

std::uint64_t Benchmark::cpuMultiChecksum(int threads, std::uint64_t unitsEach) {
    std::vector<std::uint64_t> out(static_cast<std::size_t>(threads), 0);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] { out[static_cast<std::size_t>(t)] = cpuChecksum(unitsEach, static_cast<std::uint64_t>(t) + 1); });
    }
    for (auto& th : pool) th.join();
    std::uint64_t acc = 0;
    for (const auto v : out) acc ^= v;
    return acc;
}

std::vector<std::uint32_t> Benchmark::makeCycle(std::size_t n, std::uint64_t seed) {
    std::vector<std::uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::mt19937_64 rng(seed);
    std::shuffle(order.begin(), order.end(), rng);
    std::vector<std::uint32_t> next(n);
    for (std::size_t i = 0; i < n; ++i) next[order[i]] = order[(i + 1) % n];  // Sattolo-equivalent: a single cycle through all slots
    return next;
}

Benchmark::Benchmark(BenchParams params) : params_(std::move(params)) {
    if (params_.tempDir.empty()) params_.tempDir = defaultBenchDir();
    if (params_.threads <= 0) params_.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
}

Benchmark::~Benchmark() { cleanup(); }

fs::path Benchmark::tempFile() const { return params_.tempDir / ("pmp-bench-" + std::to_string(::getpid()) + ".tmp"); }

void Benchmark::cleanup() {
    if (!file_.empty()) {
        std::error_code ec;
        fs::remove(file_, ec);
    }
    file_.clear();
    fileComplete_ = false;
}

void Benchmark::removeStaleTempFiles(const fs::path& dir) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const auto n = e.path().filename().string();
        if (!n.starts_with("pmp-bench-") || !n.ends_with(".tmp")) continue;
        // pmp-bench-<pid>.tmp: a file whose process is still running belongs to a test in progress (another window).
        const auto digits = n.substr(10, n.size() - 14);
        long pid = 0;
        if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            pid = std::strtol(digits.c_str(), nullptr, 10);
            if (pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM)) continue;
        }
        fs::remove(e.path(), ec);
    }
}

std::string Benchmark::preflight(BenchId id, const BenchParams& params) {
    const auto dir = params.tempDir.empty() ? defaultBenchDir() : params.tempDir;
    auto gb = [](double bytes) {
        char b[32];
        std::snprintf(b, sizeof b, "%.1f GB", bytes / 1e9);
        return std::string(b);
    };
    if (id == BenchId::DiskSeqWrite || id == BenchId::DiskSeqRead || id == BenchId::DiskRandRead) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        struct statvfs sv{};
        if (ec || ::statvfs(dir.c_str(), &sv) != 0) return "Cannot use " + dir.string() + " for the disk test.";
        if (::access(dir.c_str(), W_OK) != 0) return "Cannot write to " + dir.string() + ".";
        const double freeBytes = static_cast<double>(sv.f_bavail) * static_cast<double>(sv.f_frsize);
        const double need = static_cast<double>(params.diskBytes) + static_cast<double>(1ull << 30);  // the file plus a 1 GiB margin
        if (freeBytes < need) return "Needs " + gb(need) + " free in " + dir.string() + "; only " + gb(freeBytes) + " is available.";
    }
    if (id == BenchId::MemBandwidth || id == BenchId::MemLatency) {
        const auto info = readText("/proc/meminfo");
        double avail = 0;
        const auto at = info.text.find("MemAvailable:");
        if (at != std::string::npos) avail = std::strtod(info.text.c_str() + at + 13, nullptr) * 1024.0;
        const double need = 2.0 * static_cast<double>(std::max(params.memBytes, params.latencyBytes));
        if (avail > 0 && avail < need) return "Needs " + gb(need) + " of free memory; only " + gb(avail) + " is available.";
    }
    return {};
}

std::uint64_t Benchmark::estimatedSeconds(BenchId id, const BenchParams& p) {
    switch (id) {
        case BenchId::CpuSingle:
        case BenchId::CpuMulti:
        case BenchId::MemBandwidth: return static_cast<std::uint64_t>(p.seconds + 1);
        case BenchId::MemLatency: return 4;
        case BenchId::DiskSeqWrite: return static_cast<std::uint64_t>(static_cast<double>(p.diskBytes) / 150e6) + 2;
        case BenchId::DiskSeqRead: return static_cast<std::uint64_t>(static_cast<double>(p.diskBytes) / 300e6) + 2;
        case BenchId::DiskRandRead: return static_cast<std::uint64_t>(p.randomSeconds) + 1;
    }
    return 5;
}

BenchResult Benchmark::run(BenchId id, const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    const auto start = clock::now();
    BenchResult r;
    switch (id) {
        case BenchId::CpuSingle: r = cpuSingle(cancel, progress); break;
        case BenchId::CpuMulti: r = cpuMulti(cancel, progress); break;
        case BenchId::MemBandwidth: r = memBandwidth(cancel, progress); break;
        case BenchId::MemLatency: r = memLatency(cancel, progress); break;
        case BenchId::DiskSeqWrite: r = diskWrite(cancel, progress); break;
        case BenchId::DiskSeqRead: r = diskRead(cancel, progress); break;
        case BenchId::DiskRandRead: r = diskRandom(cancel, progress); break;
    }
    r.id = id;
    r.rawUnit = rawUnitOf(id);
    r.seconds = secondsSince(start);
    r.score = r.cancelled ? 0 : scoreFor(id, r.raw);
    if (r.cancelled) r.raw = 0;
    return r;
}

BenchResult Benchmark::cpuSingle(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    std::uint64_t state = 0x9E3779B97F4A7C15ULL | 1, acc = 0, units = 0;
    const auto start = clock::now();
    while (secondsSince(start) < params_.seconds) {
        if (cancel) {
            r.cancelled = true;
            return r;
        }
        for (int i = 0; i < 64; ++i) acc ^= cpuUnit(state) + units++;
        if (progress) progress(std::min(1.0, secondsSince(start) / params_.seconds));
    }
    const double elapsed = secondsSince(start);
    r.raw = static_cast<double>(units) * kOpsPerUnit / elapsed / 1e6;
    r.checksum = acc;
    if (progress) progress(1.0);
    return r;
}

BenchResult Benchmark::cpuMulti(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    const int n = params_.threads;
    std::vector<std::uint64_t> units(static_cast<std::size_t>(n), 0), sums(static_cast<std::size_t>(n), 0);
    std::atomic<bool> stop{false};
    const auto start = clock::now();
    std::vector<std::thread> pool;
    for (int t = 0; t < n; ++t) {
        pool.emplace_back([&, t] {
            std::uint64_t state = (static_cast<std::uint64_t>(t) + 1) * 0x9E3779B97F4A7C15ULL | 1, acc = 0, u = 0;
            while (!stop) {
                for (int i = 0; i < 32; ++i) acc ^= cpuUnit(state) + u++;
            }
            units[static_cast<std::size_t>(t)] = u;
            sums[static_cast<std::size_t>(t)] = acc;
        });
    }
    while (secondsSince(start) < params_.seconds && !cancel) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (progress) progress(std::min(1.0, secondsSince(start) / params_.seconds));
    }
    stop = true;
    for (auto& th : pool) th.join();
    if (cancel) {
        r.cancelled = true;
        return r;
    }
    const double elapsed = secondsSince(start);
    double total = 0;
    for (std::size_t t = 0; t < units.size(); ++t) {
        total += static_cast<double>(units[t]);
        r.checksum ^= sums[t];
    }
    r.raw = total * kOpsPerUnit / elapsed / 1e6;
    r.note = std::to_string(n) + " threads";
    if (progress) progress(1.0);
    return r;
}

BenchResult Benchmark::memBandwidth(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    const std::size_t n = params_.memBytes / (3 * sizeof(double));
    std::vector<double> a(n, 0.0), b(n), c(n);
    for (std::size_t i = 0; i < n; ++i) {
        b[i] = static_cast<double>(i % 1024) * 0.5;
        c[i] = static_cast<double>(i % 777) * 0.25;
    }
    const double s = 3.0;
    for (std::size_t i = 0; i < n; ++i) a[i] = b[i] + s * c[i];  // one untimed pass: the first touch of `a` is page faults, not bandwidth
    const auto start = clock::now();
    std::uint64_t passes = 0;
    while (passes == 0 || secondsSince(start) < params_.seconds) {
        if (cancel) {
            r.cancelled = true;
            return r;
        }
        for (std::size_t i = 0; i < n; ++i) a[i] = b[i] + s * c[i];  // the STREAM triad: two reads and one write per element
        ++passes;
        if (progress) progress(std::min(1.0, secondsSince(start) / params_.seconds));
    }
    const double elapsed = secondsSince(start);
    for (std::size_t i = 0; i < n; i += 4099) {  // verify a sample of the result, so the work cannot be optimised away
        if (a[i] != b[i] + s * c[i]) {
            r.note = "data mismatch";
            return r;
        }
    }
    double sum = 0;
    for (std::size_t i = 0; i < n; i += 64) sum += a[i];
    r.checksum = static_cast<std::uint64_t>(sum);
    r.raw = static_cast<double>(passes) * 3.0 * sizeof(double) * static_cast<double>(n) / elapsed / 1e9;
    if (progress) progress(1.0);
    return r;
}

BenchResult Benchmark::memLatency(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    struct alignas(64) Node {
        std::uint32_t next;
    };
    const std::size_t n = std::max<std::size_t>(1024, params_.latencyBytes / sizeof(Node));
    const auto cycle = makeCycle(n, 12345);
    std::vector<Node> nodes(n);
    for (std::size_t i = 0; i < n; ++i) nodes[i].next = cycle[i];
    std::uint32_t at = 0;
    const std::uint64_t steps = std::max<std::uint64_t>(2'000'000, n * 2);
    const auto start = clock::now();
    std::uint64_t done = 0;
    while (done < steps) {
        if (cancel) {
            r.cancelled = true;
            return r;
        }
        for (int i = 0; i < 100'000 && done < steps; ++i, ++done) at = nodes[at].next;  // each load depends on the previous
        if (progress) progress(static_cast<double>(done) / static_cast<double>(steps));
    }
    const double elapsed = secondsSince(start);
    r.checksum = (static_cast<std::uint64_t>(at) << 32) ^ steps;
    r.raw = elapsed / static_cast<double>(steps) * 1e9;
    return r;
}

namespace {

int openDirect(const fs::path& p, int flags, bool* buffered) {
    int fd = ::open(p.c_str(), flags | O_DIRECT | O_CLOEXEC, 0600);
    if (fd >= 0) return fd;
    if (errno == EINVAL) {  // tmpfs and some network file systems refuse O_DIRECT: fall back to buffered I/O
        *buffered = true;
        fd = ::open(p.c_str(), flags | O_CLOEXEC, 0600);
    }
    return fd;
}

void fillChunk(unsigned char* buf, std::uint64_t index) {
    std::uint64_t s = 0x1234567ULL + index;
    for (std::size_t i = 0; i < kChunk; i += 8) {
        const std::uint64_t v = xorshiftStar(s);
        std::memcpy(buf + i, &v, 8);
    }
}

}  // namespace

BenchResult Benchmark::diskWrite(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    std::error_code ec;
    fs::create_directories(params_.tempDir, ec);
    cleanup();
    file_ = tempFile();
    buffered_ = false;
    const int fd = openDirect(file_, O_WRONLY | O_CREAT | O_TRUNC, &buffered_);
    if (fd < 0) {
        r.note = "Could not create the test file: " + std::string(std::strerror(errno));
        cleanup();
        return r;
    }
    AlignedBuf buf(kChunk);
    if (!buf.p) {
        ::close(fd);
        cleanup();
        r.note = "Out of memory";
        return r;
    }
    const std::uint64_t chunks = std::max<std::uint64_t>(1, params_.diskBytes / kChunk);
    const auto start = clock::now();
    for (std::uint64_t c = 0; c < chunks; ++c) {
        if (cancel) {
            ::close(fd);
            cleanup();  // a cancelled test never leaves a big file behind
            r.cancelled = true;
            return r;
        }
        fillChunk(static_cast<unsigned char*>(buf.p), c);
        const auto written = ::write(fd, buf.p, kChunk);
        if (written != static_cast<ssize_t>(kChunk)) {
            r.note = "Write failed: " + std::string(written < 0 ? std::strerror(errno) : "short write");
            ::close(fd);
            cleanup();
            return r;
        }
        if (progress && (c % 8 == 0)) progress(static_cast<double>(c) / static_cast<double>(chunks));
    }
    ::fdatasync(fd);
    const double elapsed = secondsSince(start);
    ::close(fd);
    fileComplete_ = true;
    r.raw = static_cast<double>(chunks * kChunk) / elapsed / 1e6;
    r.checksum = chunks;
    if (buffered_) r.note = "buffered I/O: this file system refuses O_DIRECT";
    if (progress) progress(1.0);
    return r;
}

BenchResult Benchmark::diskRead(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    if (!fileComplete_) {
        // The read test needs the file the write test leaves; make it (untimed) when the write test did not run.
        BenchResult w = diskWrite(cancel, {});
        if (w.cancelled || !fileComplete_) {
            r.cancelled = w.cancelled;
            r.note = w.note;
            return r;
        }
    }
    bool buffered = buffered_;
    const int fd = openDirect(file_, O_RDONLY, &buffered);
    if (fd < 0) {
        r.note = "Could not read the test file: " + std::string(std::strerror(errno));
        return r;
    }
    if (buffered) ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);  // do not read it back from the page cache
    AlignedBuf buf(kChunk), expect(kChunk);
    const std::uint64_t chunks = std::max<std::uint64_t>(1, params_.diskBytes / kChunk);
    const auto start = clock::now();
    bool mismatch = false;
    for (std::uint64_t c = 0; c < chunks; ++c) {
        if (cancel) {
            ::close(fd);
            cleanup();
            r.cancelled = true;
            return r;
        }
        const auto got = ::read(fd, buf.p, kChunk);
        if (got != static_cast<ssize_t>(kChunk)) {
            r.note = "Read failed";
            ::close(fd);
            return r;
        }
        fillChunk(static_cast<unsigned char*>(expect.p), c);
        if (std::memcmp(buf.p, expect.p, kChunk) != 0) mismatch = true;  // verification time is tiny next to the disk
        if (progress && (c % 8 == 0)) progress(static_cast<double>(c) / static_cast<double>(chunks));
    }
    const double elapsed = secondsSince(start);
    ::close(fd);
    if (mismatch) {
        r.note = "data mismatch";  // never a fast fake score
        return r;
    }
    r.raw = static_cast<double>(chunks * kChunk) / elapsed / 1e6;
    r.checksum = chunks;
    if (buffered) r.note = "buffered I/O: this file system refuses O_DIRECT";
    if (progress) progress(1.0);
    return r;
}

BenchResult Benchmark::diskRandom(const std::atomic<bool>& cancel, const std::function<void(double)>& progress) {
    BenchResult r;
    if (!fileComplete_) {
        BenchResult w = diskWrite(cancel, {});
        if (w.cancelled || !fileComplete_) {
            r.cancelled = w.cancelled;
            r.note = w.note;
            return r;
        }
    }
    bool buffered = buffered_;
    const int fd = openDirect(file_, O_RDONLY, &buffered);
    if (fd < 0) {
        r.note = "Could not read the test file: " + std::string(std::strerror(errno));
        return r;
    }
    if (buffered) ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    AlignedBuf buf(kBlock);
    const std::uint64_t blocks = std::max<std::uint64_t>(1, (params_.diskBytes / kChunk) * kChunk / kBlock);
    std::mt19937_64 rng(777);
    const auto start = clock::now();
    std::uint64_t ops = 0;
    while (secondsSince(start) < params_.randomSeconds) {
        if (cancel) {
            ::close(fd);
            cleanup();
            r.cancelled = true;
            return r;
        }
        for (int i = 0; i < 64; ++i) {  // queue depth 1: each read finishes before the next starts
            const auto off = static_cast<off_t>((rng() % blocks) * kBlock);
            if (::pread(fd, buf.p, kBlock, off) != static_cast<ssize_t>(kBlock)) {
                r.note = "Read failed";
                ::close(fd);
                return r;
            }
            ++ops;
        }
        if (progress) progress(std::min(1.0, secondsSince(start) / params_.randomSeconds));
    }
    const double elapsed = secondsSince(start);
    ::close(fd);
    r.raw = static_cast<double>(ops) / elapsed;
    r.checksum = ops;
    if (buffered) r.note = "buffered I/O: this file system refuses O_DIRECT";
    return r;
}

}  // namespace pmp::linux
