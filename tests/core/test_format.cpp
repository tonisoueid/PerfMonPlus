// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <limits>
#include <perfmonplus/core/format.hpp>

#include <catch2/catch_test_macros.hpp>

using pmp::Availability;
using pmp::Metric;
using pmp::Unit;

namespace {
constexpr double KiB = 1024.0;
constexpr double MiB = KiB * 1024;
constexpr double GiB = MiB * 1024;
constexpr double TiB = GiB * 1024;
Metric m(double v, Unit u) { return Metric::measured(v, u); }
}  // namespace

TEST_CASE("format distinguishes measured zero from unavailable", "[format]") {
    CHECK(pmp::format(m(0, Unit::BytesPerSec)) == "Zero KB/s");
    CHECK(pmp::format(m(0, Unit::Bytes)) == "Zero KB");
    CHECK(pmp::format(Metric::notMeasured(Availability::Unsupported, Unit::Bytes)) == "Unavailable");
    CHECK(pmp::format(Metric::notMeasured(Availability::Failed, Unit::Bytes)) == "Unavailable");
    CHECK(pmp::format(Metric::notMeasured(Availability::Restricted, Unit::Watts)) == "Restricted");
    CHECK(pmp::format(Metric::notMeasured(Availability::Pending, Unit::Percent)) == "—");
    CHECK(pmp::format(Metric::notMeasured(Availability::Stale, Unit::Percent)) == "—");
    CHECK(pmp::format(Metric::notMeasured(Availability::Unsupported, Unit::Hertz, "Auto")) == "Auto");
}

TEST_CASE("format scales byte units", "[format]") {
    CHECK(pmp::format(m(500, Unit::Bytes)) == "<1 KB");
    CHECK(pmp::format(m(22.0 * KiB, Unit::BytesPerSec)) == "22 KB/s");
    CHECK(pmp::format(m(885.8 * MiB, Unit::Bytes)) == "885.8 MB");
    CHECK(pmp::format(m(1019.5 * MiB, Unit::Bytes)) == "1,019.5 MB");
    CHECK(pmp::format(m(3.35 * GiB, Unit::Bytes)) == "3.35 GB");
    CHECK(pmp::format(m(128.0 * GiB, Unit::Bytes)) == "128 GB");
    CHECK(pmp::format(m(1.03 * TiB, Unit::Bytes)) == "1.03 TB");
}

TEST_CASE("format other units", "[format]") {
    CHECK(pmp::format(m(15.8, Unit::Percent)) == "15.8%");
    CHECK(pmp::format(m(0, Unit::Percent)) == "0.0%");
    CHECK(pmp::format(m(37.5, Unit::Celsius)) == "37.5 °C");
    CHECK(pmp::format(m(111.4, Unit::Watts)) == "111 W");
    CHECK(pmp::format(m(152435342, Unit::Count)) == "152,435,342");
    CHECK(pmp::format(m(0.0, Unit::Milliseconds)) == "0.00 ms");
    CHECK(pmp::format(m(3.2e9, Unit::Hertz)) == "3.20 GHz");
    CHECK(pmp::format(m(485479, Unit::Seconds)) == "5:14:51:19");
}

TEST_CASE("rpm unit", "[format]") {
    CHECK(pmp::format(m(2400, Unit::Rpm)) == "2,400 RPM");
    CHECK(pmp::toString(Unit::Rpm) == "rpm");
}

TEST_CASE("fahrenheit option", "[format]") {
    pmp::setFormatOptions({.fahrenheit = true});
    CHECK(pmp::format(m(37.5, Unit::Celsius)) == "99.5 °F");
    pmp::setFormatOptions({});
    CHECK(pmp::format(m(37.5, Unit::Celsius)) == "37.5 °C");
}

TEST_CASE("decimal bytes option", "[format]") {
    pmp::setFormatOptions({.binaryBytes = false});
    CHECK(pmp::format(m(1.5e6, Unit::Bytes)) == "1.5 MB");
    CHECK(pmp::format(m(1e9, Unit::Bytes)) == "1 GB");
    pmp::setFormatOptions({});
    CHECK(pmp::format(m(1.5 * MiB, Unit::Bytes)) == "1.5 MB");
}

TEST_CASE("a value that rounds up to the next unit is shown in that unit", "[format][minor]") {
    CHECK(pmp::format(m(1048575, Unit::Bytes)) == "1.0 MB");              // not "1,024 KB"
    CHECK(pmp::format(m(1023.6 * 1024, Unit::Bytes)) == "1.0 MB");
    CHECK(pmp::format(m(1024.0 * 1024 * 1024 - 1, Unit::Bytes)) == "1 GB");  // 1023.99999 MB rounds to 1024.0
    CHECK(pmp::format(m(1023.4 * 1024, Unit::Bytes)) == "1,023 KB");
    pmp::setFormatOptions({.binaryBytes = false});
    CHECK(pmp::format(m(999999, Unit::Bytes)) == "1.0 MB");
    pmp::setFormatOptions({});
}

TEST_CASE("numbers that cannot be real readings are never shown as numbers", "[format][minor]") {
    CHECK(pmp::format(m(std::numeric_limits<double>::quiet_NaN(), Unit::Percent)) == "Unavailable");
    CHECK(pmp::format(m(std::numeric_limits<double>::infinity(), Unit::Bytes)) == "Unavailable");
    CHECK(pmp::format(m(-std::numeric_limits<double>::infinity(), Unit::Watts)) == "Unavailable");
    CHECK(pmp::format(m(-5, Unit::Bytes)) == "Unavailable");
    CHECK(pmp::format(m(-5, Unit::BytesPerSec)) == "Unavailable");
    CHECK(pmp::format(m(-5, Unit::Celsius)) == "-5.0 °C");  // below zero is a real temperature
}
