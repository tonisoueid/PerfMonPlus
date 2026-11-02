// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/memory_provider.hpp>

#include <charconv>
#include <optional>

namespace pmp::linux {
namespace {

using KV = std::map<std::string, std::uint64_t, std::less<>>;

std::optional<double> find(const KV& m, std::string_view key) {
    const auto it = m.find(key);
    if (it == m.end()) return std::nullopt;
    return static_cast<double>(it->second);
}

Metric bytesFromKb(const KV& m, std::string_view key) {
    if (auto v = find(m, key)) return Metric::measured(*v * 1024.0, Unit::Bytes);
    return Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
}

Metric difference(const Metric& a, const Metric& b) {
    if (a.isMeasured() && b.isMeasured()) return Metric::measured(*a.value - *b.value, Unit::Bytes);
    return Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
}

Metric psiSomeAvg10(const ProcRoot& root) {
    const auto f = readText(root.proc / "pressure" / "memory");
    if (!f.ok()) return Metric::notMeasured(availabilityForErrno(f.error), Unit::Percent);
    const auto some = f.text.find("some ");
    const auto key = f.text.find("avg10=", some == std::string::npos ? 0 : some);
    if (some == std::string::npos || key == std::string::npos) {
        return Metric::notMeasured(Availability::Failed, Unit::Percent);
    }
    const char* b = f.text.data() + key + 6;
    double v = 0;
    const auto r = std::from_chars(b, f.text.data() + f.text.size(), v);
    if (r.ec != std::errc{}) return Metric::notMeasured(Availability::Failed, Unit::Percent);
    return Metric::measured(v, Unit::Percent);
}

}  // namespace

void MemoryProvider::sample(Frame& out, const SampleContext&) {
    auto& m = out.memory;
    const auto info = readText(root_.proc / "meminfo");
    if (!info.ok()) {
        fillUnavailable(out, availabilityForErrno(info.error));
        return;
    }
    const KV kv = parseColonValues(info.text);

    m.total = bytesFromKb(kv, "MemTotal");
    m.available = bytesFromKb(kv, "MemAvailable");
    m.inUse = difference(m.total, m.available);
    m.committed = bytesFromKb(kv, "Committed_AS");
    m.buffers = bytesFromKb(kv, "Buffers");
    m.compressed = bytesFromKb(kv, "Zswap");

    if (auto cached = find(kv, "Cached")) {
        m.cached = Metric::measured((*cached + find(kv, "SReclaimable").value_or(0)) * 1024.0, Unit::Bytes);
    } else {
        m.cached = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
    }

    m.swapTotal = bytesFromKb(kv, "SwapTotal");
    m.swapAvailable = bytesFromKb(kv, "SwapFree");
    m.swapUsed = difference(m.swapTotal, m.swapAvailable);

    m.pressureSomeAvg10 = psiSomeAvg10(root_);

    const auto vm = readText(root_.proc / "vmstat");
    if (vm.ok()) {
        const KV v = parseSpaceValues(vm.text);
        const auto count = [&](std::string_view k) {
            if (auto x = find(v, k)) return Metric::measured(*x, Unit::Count);
            return Metric::notMeasured(Availability::Unsupported, Unit::Count);
        };
        m.pageIns = count("pgpgin");
        m.pageOuts = count("pgpgout");
    } else {
        m.pageIns = m.pageOuts = Metric::notMeasured(availabilityForErrno(vm.error), Unit::Count);
    }
}

void MemoryProvider::fillUnavailable(Frame& out, Availability why) const {
    auto& m = out.memory;
    const auto bytes = Metric::notMeasured(why, Unit::Bytes);
    m.total = m.inUse = m.available = m.committed = m.cached = m.buffers = m.swapTotal = m.swapUsed =
        m.swapAvailable = m.compressed = bytes;
    m.pressureSomeAvg10 = Metric::notMeasured(why, Unit::Percent);
    m.pageIns = m.pageOuts = Metric::notMeasured(why, Unit::Count);
}

}  // namespace pmp::linux
