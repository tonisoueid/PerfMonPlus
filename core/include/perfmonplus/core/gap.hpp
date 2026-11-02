// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <chrono>

namespace pmp {

// Should history show a break between the previous sample and this one?
//   monoElapsed: since the previous sample, CLOCK_MONOTONIC (pauses in suspend)
//   bootElapsed: since the previous sample, CLOCK_BOOTTIME (keeps counting)
// Never call for the first sample.
bool shouldInsertGap(std::chrono::nanoseconds expectedInterval, std::chrono::nanoseconds monoElapsed,
                     std::chrono::nanoseconds bootElapsed);

}  // namespace pmp
