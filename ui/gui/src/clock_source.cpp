// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/clock_source.hpp>

namespace pmp::gui {
namespace {
std::optional<MonoTime> g_override;
}

MonoTime graphNow() { return g_override ? *g_override : std::chrono::steady_clock::now(); }
void setGraphClockOverride(std::optional<MonoTime> now) { g_override = now; }

}  // namespace pmp::gui
