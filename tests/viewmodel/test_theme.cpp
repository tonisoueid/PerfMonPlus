// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/theme.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp::vm;

namespace {
double luma(Rgb c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }
}  // namespace

TEST_CASE("saturation 7 is identity", "[theme]") {
    REQUIRE(applySaturation({200, 50, 50}, 7) == Rgb{200, 50, 50});
}

TEST_CASE("saturation 0 is grey", "[theme]") {
    const auto c = applySaturation({200, 50, 50}, 0);
    REQUIRE(c.r == c.g);
    REQUIRE(c.g == c.b);
}

TEST_CASE("saturation 11 is more vivid than 7", "[theme]") {
    const auto a = applySaturation({200, 100, 100}, 7);
    const auto b = applySaturation({200, 100, 100}, 11);
    REQUIRE((b.r - b.g) > (a.r - a.g));
}

TEST_CASE("saturation clamps channels", "[theme]") {
    const auto c = applySaturation({250, 10, 10}, 11);
    REQUIRE(c.r == 255);
    REQUIRE(c.g == 0);
}

TEST_CASE("themes are distinct", "[theme]") {
    REQUIRE(darkTheme().background != lightTheme().background);
    REQUIRE(luma(darkTheme().background) < luma(lightTheme().background));
}

TEST_CASE("green phosphor uses one hue", "[theme]") {
    const auto t = greenPhosphorTheme();
    REQUIRE(t.phosphor);
    for (const Rgb c : {t.cpu, t.cpuKernel, t.temperature, t.memory, t.disk, t.netRx, t.netTx, t.energy}) {
        REQUIRE(c.g >= c.r);
        REQUIRE(c.g >= c.b);
    }
}

TEST_CASE("themeByName finds built-ins", "[theme]") {
    REQUIRE(themeByName("green")->name == "green");
    REQUIRE(themeByName("dark")->name == "dark");
    REQUIRE(themeByName("light")->name == "light");
    REQUIRE_FALSE(themeByName("nope").has_value());
}

TEST_CASE("withSaturation leaves phosphor alone and greys colour themes", "[theme]") {
    REQUIRE(withSaturation(greenPhosphorTheme(), 0).cpu == greenPhosphorTheme().cpu);
    const auto grey = withSaturation(darkTheme(), 0).cpu;
    REQUIRE(grey.r == grey.g);
    REQUIRE(grey.g == grey.b);
}

TEST_CASE("saturation 0 leaves every subsystem colour grey", "[theme][minor]") {
    for (const auto& base : {pmp::vm::darkTheme(), pmp::vm::lightTheme()}) {
        const auto t = pmp::vm::withSaturation(base, 0);
        for (const auto& c : {t.cpu, t.cpuKernel, t.temperature, t.memory, t.disk, t.netRx, t.netTx, t.energy}) {
            CHECK(c.r == c.g);
            CHECK(c.g == c.b);
        }
    }
}
