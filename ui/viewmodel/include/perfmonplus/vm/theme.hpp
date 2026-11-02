// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/vm/color.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace pmp::vm {

struct Theme {
    std::string name;  // "dark" | "light" | "green"
    bool phosphor = false;
    Rgb background{}, panel{}, text{}, textDim{}, accent{}, grid{}, border{}, ok{}, warn{};
    Rgb cpu{}, cpuKernel{}, temperature{}, memory{}, disk{}, netRx{}, netTx{}, energy{};
};

Theme darkTheme();
Theme lightTheme();
Theme greenPhosphorTheme();
std::optional<Theme> themeByName(std::string_view name);

// Applies to the subsystem colours only; phosphor themes are single-hue and unchanged.
Theme withSaturation(const Theme& t, int saturation);

}  // namespace pmp::vm
