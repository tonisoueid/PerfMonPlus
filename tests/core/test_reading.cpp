// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/reading.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

TEST_CASE("default metric is pending, not zero", "[reading]") {
    pmp::Metric m;
    REQUIRE(m.availability == pmp::Availability::Pending);
    REQUIRE_FALSE(m.value.has_value());
}

TEST_CASE("measured zero keeps its value", "[reading]") {
    auto m = pmp::Metric::measured(0.0, pmp::Unit::BytesPerSec);
    REQUIRE(m.isMeasured());
    REQUIRE(m.value == 0.0);
}

TEST_CASE("not-measured readings carry no value and keep label", "[reading]") {
    auto m = pmp::Metric::notMeasured(pmp::Availability::Unsupported, pmp::Unit::Hertz, "Auto");
    REQUIRE_FALSE(m.value.has_value());
    REQUIRE(m.label == "Auto");
    REQUIRE_THROWS_AS(pmp::Metric::notMeasured(pmp::Availability::Measured, pmp::Unit::Count),
                      std::invalid_argument);
}

TEST_CASE("availability and unit names", "[reading]") {
    REQUIRE(pmp::toString(pmp::Availability::Restricted) == "restricted");
    REQUIRE(pmp::toString(pmp::Unit::BytesPerSec) == "bytes_per_sec");
}
