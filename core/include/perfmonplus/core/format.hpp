// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>

#include <string>

namespace pmp {

// The only place a Metric becomes display text (DM-01). A measured zero
// renders as a zero ("Zero KB/s"); anything not measured never does.
std::string format(const Metric& m);

// Process-wide display options (set from the UI thread only).
struct FormatOptions {
    bool binaryBytes = true;  // false: base-1000 sizes (labels stay KB/MB/GB)
    bool fahrenheit = false;
};
void setFormatOptions(FormatOptions o);
FormatOptions formatOptions();

}  // namespace pmp
