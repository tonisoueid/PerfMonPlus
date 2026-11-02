// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>

#include <optional>

namespace pmp::gui {

// The instant graphs scroll to between samples. Live, that is the steady clock (the sampler's clock); while a
// recording is replayed it is the playhead, so a paused replay stands still and a scrubbed one shows what was
// recorded. GUI thread only.
MonoTime graphNow();
void setGraphClockOverride(std::optional<MonoTime> now);

}  // namespace pmp::gui
