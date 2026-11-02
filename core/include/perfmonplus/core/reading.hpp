// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace pmp {

enum class Availability { Measured, Unsupported, Restricted, Pending, Stale, Failed, Disabled };

enum class Unit {
    Percent,
    Bytes,
    BytesPerSec,
    Watts,
    Celsius,
    Hertz,
    Count,
    Seconds,
    Milliseconds,
    Rpm,
    Volts,
    WattHours
};

using MonoTime = std::chrono::steady_clock::time_point;

// A single reading. `value` is engaged iff availability == Measured, so a
// missing sensor can never be mistaken for a measured zero (INT-01/02).
template <class T>
struct Reading {
    std::optional<T> value;
    Availability availability = Availability::Pending;
    Unit unit = Unit::Count;
    std::string label;

    static Reading measured(T v, Unit u) {
        Reading r;
        r.value = std::move(v);
        r.availability = Availability::Measured;
        r.unit = u;
        return r;
    }

    static Reading notMeasured(Availability a, Unit u, std::string label = {}) {
        if (a == Availability::Measured) {
            throw std::invalid_argument("notMeasured() called with Availability::Measured");
        }
        Reading r;
        r.availability = a;
        r.unit = u;
        r.label = std::move(label);
        return r;
    }

    bool isMeasured() const noexcept { return availability == Availability::Measured; }
};

using Metric = Reading<double>;

constexpr std::string_view toString(Availability a) {
    switch (a) {
        case Availability::Measured: return "measured";
        case Availability::Unsupported: return "unsupported";
        case Availability::Restricted: return "restricted";
        case Availability::Pending: return "pending";
        case Availability::Stale: return "stale";
        case Availability::Disabled: return "disabled";
        case Availability::Failed: return "failed";
    }
    return "failed";
}

constexpr std::string_view toString(Unit u) {
    switch (u) {
        case Unit::Percent: return "percent";
        case Unit::Bytes: return "bytes";
        case Unit::BytesPerSec: return "bytes_per_sec";
        case Unit::Watts: return "watts";
        case Unit::Celsius: return "celsius";
        case Unit::Hertz: return "hertz";
        case Unit::Count: return "count";
        case Unit::Seconds: return "seconds";
        case Unit::Milliseconds: return "milliseconds";
        case Unit::Rpm: return "rpm";
        case Unit::Volts: return "volts";
        case Unit::WattHours: return "watt_hours";
    }
    return "count";
}

}  // namespace pmp
