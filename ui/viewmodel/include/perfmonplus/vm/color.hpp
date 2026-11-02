// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <cstdint>

namespace pmp::vm {

struct Rgb {
    std::uint8_t r, g, b;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// 0..11; 7 = the colours as designed, 0 = grey (same luma), 11 = boosted. Clamps.
Rgb applySaturation(Rgb c, int saturation);

}  // namespace pmp::vm
