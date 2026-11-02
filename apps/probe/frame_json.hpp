// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace pmp::probe {

// {"value": number|null, "availability": "...", "unit": "..."}
nlohmann::json metricToJson(const Metric& m);

// One generation as a JSON object; `top` live processes by CPU (pending last).
nlohmann::json toJson(const Frame& f, std::size_t topN, bool gapBefore);

// One compact line. Kernel-supplied strings (e.g. comm) may be invalid UTF-8,
// which dump() would otherwise throw on; bad bytes become U+FFFD instead.
std::string dumpLine(const nlohmann::json& j);

}  // namespace pmp::probe
