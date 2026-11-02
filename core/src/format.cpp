// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/format.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace pmp {
namespace {

FormatOptions g_options;

double base() { return g_options.binaryBytes ? 1024.0 : 1000.0; }

// "1019.5" -> "1,019.5": group only the integer part.
std::string groupThousands(std::string s) {
    const auto dot = s.find('.');
    const std::size_t intEnd = dot == std::string::npos ? s.size() : dot;
    const std::size_t intStart = (!s.empty() && s[0] == '-') ? 1 : 0;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(intEnd) - 3;
         i > static_cast<std::ptrdiff_t>(intStart); i -= 3) {
        s.insert(static_cast<std::size_t>(i), ",");
    }
    return s;
}

std::string fixed(double v, int decimals) {
    return groupThousands(std::format("{:.{}f}", v, decimals));
}

std::string formatBytes(double bytes, std::string_view suffix) {
    const double kKiB = base();
    if (bytes == 0.0) return std::format("Zero KB{}", suffix);
    if (bytes < kKiB) return std::format("<1 KB{}", suffix);

    // KB, MB, GB, TB with 0, 1, 2, 2 decimals. A value that rounds up to the next unit's threshold ("1,024 KB") is
    // promoted, so the number shown is never 1,024 of the unit below.
    static constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
    static constexpr int kDecimals[] = {0, 1, 2, 2};
    int level = 0;
    double v = bytes / kKiB;
    while (level < 3 && (v >= kKiB || std::round(v * std::pow(10.0, kDecimals[level])) >= kKiB * std::pow(10.0, kDecimals[level]))) {
        v /= kKiB;
        ++level;
    }
    const char* unit = kUnits[level];
    const int decimals = kDecimals[level];

    std::string num = fixed(v, decimals);
    if (decimals == 2 && num.ends_with(".00")) num.resize(num.size() - 3);  // "128 GB"
    return std::format("{} {}{}", num, unit, suffix);
}

std::string formatHertz(double hz) {
    if (hz >= 1e9) return std::format("{:.2f} GHz", hz / 1e9);
    return std::format("{:.2f} MHz", hz / 1e6);
}

std::string formatDuration(double seconds) {
    auto total = static_cast<std::uint64_t>(seconds < 0 ? 0 : seconds);
    const auto d = total / 86400;
    const auto h = total % 86400 / 3600;
    const auto mi = total % 3600 / 60;
    const auto s = total % 60;
    return std::format("{}:{:02}:{:02}:{:02}", d, h, mi, s);
}

std::string unavailableText(const Metric& m) {
    if (!m.label.empty()) return m.label;
    switch (m.availability) {
        case Availability::Restricted: return "Restricted";
        case Availability::Disabled: return "Disabled";
        case Availability::Pending:
        case Availability::Stale: return "—";
        default: return "Unavailable";
    }
}

}  // namespace

void setFormatOptions(FormatOptions o) { g_options = o; }
FormatOptions formatOptions() { return g_options; }

std::string format(const Metric& m) {
    if (!m.isMeasured() || !m.value) return unavailableText(m);
    const double v = *m.value;
    if (!std::isfinite(v)) return "Unavailable";  // a garbled reading is a fault, never a number
    if ((m.unit == Unit::Bytes || m.unit == Unit::BytesPerSec) && v < 0) return "Unavailable";
    switch (m.unit) {
        case Unit::Bytes: return formatBytes(v, "");
        case Unit::BytesPerSec: return formatBytes(v, "/s");
        case Unit::Percent: return std::format("{:.1f}%", v);
        case Unit::Celsius:
            return g_options.fahrenheit ? std::format("{:.1f} °F", v * 9.0 / 5.0 + 32.0) : std::format("{:.1f} °C", v);
        case Unit::Rpm: return fixed(v, 0) + " RPM";
        case Unit::Volts: return std::format("{:.2f} V", v);
        case Unit::WattHours: return std::format("{:.1f} Wh", v);
        case Unit::Watts: return std::format("{:.0f} W", v);
        case Unit::Count: return fixed(v, 0);
        case Unit::Milliseconds: return std::format("{:.2f} ms", v);
        case Unit::Hertz: return formatHertz(v);
        case Unit::Seconds: return formatDuration(v);
    }
    return unavailableText(m);
}

}  // namespace pmp
