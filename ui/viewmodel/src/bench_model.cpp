// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/bench_model.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>

namespace pmp::vm {
namespace {

std::string grouped(long long v) {
    auto s = std::to_string(v < 0 ? -v : v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return (v < 0 ? "-" : "") + s;
}

const linux::BenchResult* find(const linux::BenchRecord& r, linux::BenchId id) {
    const linux::BenchResult* best = nullptr;
    for (const auto& x : r.results) {
        if (x.id == id && !x.cancelled && x.raw > 0) best = &x;
    }
    return best;
}

}  // namespace

std::string rawText(const linux::BenchResult& r) {
    char b[64];
    if (r.rawUnit == "GB/s" || r.rawUnit == "ns" || r.rawUnit == "Mops/s") {
        std::snprintf(b, sizeof b, "%.1f %s", r.raw, r.rawUnit.c_str());
        if (r.rawUnit == "Mops/s") return grouped(std::llround(r.raw)) + " Mops/s";
        return b;
    }
    return grouped(std::llround(r.raw)) + " " + r.rawUnit;
}

std::string dateText(std::int64_t wallMs) {
    const auto d = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::time_point(std::chrono::milliseconds(wallMs)));
    const std::chrono::year_month_day ymd{d};
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
}

std::string compareText(double now, double before, const std::string& beforeDate, bool smallerIsBetter) {
    if (before <= 0 || now <= 0) return {};
    // A positive number always means "better": faster, or for latency, lower.
    const double change = (smallerIsBetter ? before / now : now / before) - 1.0;
    const long pct = std::lround(change * 100.0);
    if (pct == 0) return "No change vs your run on " + beforeDate;
    return (pct > 0 ? "+" + std::to_string(pct) : "\xE2\x88\x92" + std::to_string(-pct)) + "% vs your run on " + beforeDate;
}

bool comparable(const linux::BenchRecord& a, const linux::BenchRecord& b) {
    return a.benchVersion == b.benchVersion && a.optimized == b.optimized && a.cpuModel == b.cpuModel && a.logicalCpus == b.logicalCpus;
}

std::vector<ResultCard> buildCards(const linux::BenchRecord& latest, const std::vector<linux::BenchRecord>& history) {
    std::vector<ResultCard> cards;
    for (const auto& r : latest.results) {
        ResultCard c;
        c.name = linux::benchName(r.id);
        c.date = dateText(latest.wallMs);
        c.note = r.note;
        c.failed = r.cancelled || r.raw <= 0;
        if (c.failed) {
            c.score = "—";
            c.raw = r.cancelled ? "Cancelled" : (r.note.empty() ? "No result" : r.note);
            c.comparison = "";
            cards.push_back(std::move(c));
            continue;
        }
        c.score = grouped(std::llround(r.score));
        c.raw = rawText(r);
        const linux::BenchRecord* prev = nullptr;
        for (const auto& h : history) {
            if (h.wallMs >= latest.wallMs || !comparable(h, latest) || !find(h, r.id)) continue;
            if (!prev || h.wallMs > prev->wallMs) prev = &h;
        }
        if (prev) {
            c.comparable = true;
            const bool smaller = r.id == linux::BenchId::MemLatency;
            c.comparison = compareText(r.raw, find(*prev, r.id)->raw, dateText(prev->wallMs), smaller);
        } else {
            c.comparison = "First run on this machine";
        }
        cards.push_back(std::move(c));
    }
    return cards;
}

std::string historyLine(const linux::BenchRecord& r) {
    std::string s = dateText(r.wallMs);
    for (const auto& x : r.results) {
        s += " · " + linux::benchName(x.id) + " " + (x.cancelled || x.raw <= 0 ? std::string("—") : grouped(std::llround(x.score)));
    }
    return s;
}

}  // namespace pmp::vm
