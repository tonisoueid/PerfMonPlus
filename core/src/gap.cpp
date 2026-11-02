// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/gap.hpp>

namespace pmp {
namespace {
// Spec §9.1: a reading older than 2x the interval is stale.
constexpr int kLateFactor = 2;
// Excess on the boot clock beyond one interval can only come from suspend;
// NTP/VM clock drift is far smaller.
constexpr int kSleepFactor = 1;
}  // namespace

bool shouldInsertGap(std::chrono::nanoseconds expectedInterval, std::chrono::nanoseconds monoElapsed,
                     std::chrono::nanoseconds bootElapsed) {
    if (expectedInterval <= std::chrono::nanoseconds::zero() ||
        monoElapsed < std::chrono::nanoseconds::zero() ||
        bootElapsed < std::chrono::nanoseconds::zero()) {
        return false;
    }
    const bool late = monoElapsed > kLateFactor * expectedInterval;
    const bool slept = bootElapsed - monoElapsed > kSleepFactor * expectedInterval;
    return late || slept;
}

}  // namespace pmp
