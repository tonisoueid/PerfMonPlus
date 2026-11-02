// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/bench_store.hpp>

#include <string>
#include <vector>

namespace pmp::vm {

// One result card (BEN-02): the score, the raw metric, the date, and how it compares with your last comparable run.
struct ResultCard {
    std::string name, score, raw, date, comparison, note;
    bool failed = false;     // cancelled, or the test found a problem (a data mismatch scores nothing)
    bool comparable = false; // there was an earlier run on this same machine and test version to compare with
};

std::string rawText(const linux::BenchResult& r);                    // "31.2 GB/s", "118.0 ns", "1,580 MB/s"
std::string dateText(std::int64_t wallMs);                           // "2026-10-25"
// "+12% vs your run on 2026-10-20", "−4% vs …" (a true minus sign), or "No change"; "" when `before` is 0.
std::string compareText(double now, double before, const std::string& beforeDate, bool smallerIsBetter);

// A run is comparable with another only on the same CPU model, logical CPU count and benchmark version (BEN-04).
bool comparable(const linux::BenchRecord& a, const linux::BenchRecord& b);

// Cards for `latest`, compared with the newest earlier comparable record in `history` (which may include `latest`).
std::vector<ResultCard> buildCards(const linux::BenchRecord& latest, const std::vector<linux::BenchRecord>& history);

// One line per past run for the history list: "2026-10-25 · CPU single-thread 4,447 · …".
std::string historyLine(const linux::BenchRecord& r);

}  // namespace pmp::vm
