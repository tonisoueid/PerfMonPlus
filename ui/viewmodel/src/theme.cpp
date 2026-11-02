// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/theme.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::vm {
namespace {

Rgb scaled(Rgb c, double k) {
    const auto ch = [&](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(v * k), 0L, 255L));
    };
    return {ch(c.r), ch(c.g), ch(c.b)};
}

}  // namespace

Theme darkTheme() {
    Theme t;
    t.name = "dark";
    t.background = {0x10, 0x14, 0x18};
    t.panel = {0x17, 0x1c, 0x22};
    t.text = {0xe6, 0xed, 0xf3};
    t.textDim = {0x8b, 0x98, 0xa5};
    t.accent = {0x3b, 0x5b, 0xdb};
    t.grid = {0x2a, 0x33, 0x3d};
    t.border = {0x2b, 0x36, 0x42};
    t.ok = {0x43, 0xd6, 0x5a};
    t.warn = {0xf5, 0xa5, 0x24};
    t.cpu = {0x43, 0xd6, 0x5a};
    t.cpuKernel = {0xe5, 0x48, 0x4d};
    t.temperature = {0xf5, 0xa5, 0x24};
    t.memory = {0xb4, 0x55, 0xf2};
    t.disk = {0x3f, 0xd1, 0x6a};
    t.netRx = {0x4d, 0xa3, 0xff};
    t.netTx = {0x7d, 0xdc, 0x7a};
    t.energy = {0xf2, 0xd3, 0x38};
    return t;
}

Theme lightTheme() {
    Theme t = darkTheme();
    t.name = "light";
    t.background = {0xf4, 0xf6, 0xf8};
    t.panel = {0xff, 0xff, 0xff};
    t.text = {0x1b, 0x21, 0x27};
    t.textDim = {0x5c, 0x68, 0x75};
    t.accent = {0x2f, 0x54, 0xd6};
    t.grid = {0xd5, 0xdb, 0xe1};
    t.border = {0xc9, 0xd1, 0xd9};
    for (Rgb* c : {&t.ok, &t.warn, &t.cpu, &t.cpuKernel, &t.temperature, &t.memory, &t.disk, &t.netRx, &t.netTx, &t.energy}) {
        *c = scaled(*c, 0.75);
    }
    return t;
}

Theme greenPhosphorTheme() {
    const Rgb base{0x35, 0xff, 0x6a};
    Theme t;
    t.name = "green";
    t.phosphor = true;
    t.background = {0x06, 0x13, 0x0a};
    t.panel = {0x0a, 0x1f, 0x10};
    t.text = scaled(base, 0.85);
    t.textDim = scaled(base, 0.5);
    t.accent = scaled(base, 0.45);
    t.grid = scaled(base, 0.18);
    t.border = scaled(base, 0.3);
    t.ok = base;
    t.warn = scaled(base, 0.7);
    t.cpu = base;
    t.cpuKernel = scaled(base, 0.6);
    t.temperature = scaled(base, 0.8);
    t.memory = scaled(base, 0.75);
    t.disk = scaled(base, 0.9);
    t.netRx = base;
    t.netTx = scaled(base, 0.7);
    t.energy = scaled(base, 0.85);
    return t;
}

std::optional<Theme> themeByName(std::string_view name) {
    if (name == "dark") return darkTheme();
    if (name == "light") return lightTheme();
    if (name == "green") return greenPhosphorTheme();
    return std::nullopt;
}

Theme withSaturation(const Theme& t, int saturation) {
    if (t.phosphor) return t;
    Theme out = t;
    for (Rgb* c : {&out.cpu, &out.cpuKernel, &out.temperature, &out.memory, &out.disk, &out.netRx, &out.netTx, &out.energy}) {
        *c = applySaturation(*c, saturation);
    }
    return out;
}

}  // namespace pmp::vm
