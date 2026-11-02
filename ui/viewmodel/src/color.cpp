// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/color.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::vm {

Rgb applySaturation(Rgb c, int saturation) {
    const double f = std::clamp(saturation, 0, 11) / 7.0;
    const double y = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
    const auto ch = [&](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(y + (v - y) * f), 0L, 255L));
    };
    return {ch(c.r), ch(c.g), ch(c.b)};
}

}  // namespace pmp::vm
