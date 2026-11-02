// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/power_freq_model.hpp>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/vm/hardware_model.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace pmp::vm {
namespace {

std::string prettify(std::string s) {  // balance_power -> "Balance power"
    for (auto& c : s) {
        if (c == '_' || c == '-') c = ' ';
    }
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string orUnavailable(const std::string& s) { return s.empty() ? "Unavailable" : s; }

std::string oneDecimal(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

}  // namespace

PowerFreqModel buildPowerFreq(const Frame& f) {
    PowerFreqModel m;
    for (const auto& c : f.cpu.logical) {
        FreqRow r;
        r.label = "CPU " + std::to_string(c.id);
        r.coreClass = c.coreClass;
        r.cur = format(c.freqHz);
        r.min = format(c.freqMinHz);
        r.max = format(c.freqMaxHz);
        r.available = c.freqHz.isMeasured();
        if (c.freqHz.isMeasured() && c.freqMaxHz.isMeasured() && *c.freqMaxHz.value > 0) {
            r.fraction = std::clamp(*c.freqHz.value / *c.freqMaxHz.value, 0.0, 1.0);
        }
        m.cores.push_back(std::move(r));
    }
    m.governor = orUnavailable(f.cpu.governor);
    m.epp = f.cpu.epp.empty() ? "Unavailable" : prettify(f.cpu.epp);
    m.driver = orUnavailable(f.cpu.driver);

    const auto& t = f.cpu.throttleEvents;
    if (t.isMeasured()) {
        const auto n = static_cast<long long>(*t.value);
        m.throttle = n == 0 ? "None" : std::to_string(n) + (n == 1 ? " event" : " events") + " since boot";
    } else if (t.availability == Availability::Unsupported) {
        m.throttle = "Not reported by this CPU";
    } else {
        m.throttle = format(t);
    }

    for (const auto& d : f.energy.domains) {
        PowerRow r;
        r.id = d.id;
        r.label = d.label;
        r.restricted = d.watts.availability == Availability::Restricted;
        if (d.watts.isMeasured()) {
            char buf[32];
            std::snprintf(buf, sizeof buf, *d.watts.value < 1.0 && *d.watts.value > 0 ? "%.1f W" : "%.0f W", *d.watts.value);
            r.value = buf;
        } else {
            r.value = format(d.watts);
        }
        m.domains.push_back(std::move(r));
    }
    if (const auto help = restrictedPowerHelp(f.energy); !help.empty()) {
        m.restrictedHelp = help + " PerfMonPlus never installs rules or runs as root for you.";
    }

    const auto& b = f.energy.battery;
    if (b.present) {
        auto& rows = m.battery.rows;
        if (b.healthPct.isMeasured()) {
            std::string v = std::to_string(static_cast<int>(*b.healthPct.value + 0.5)) + "% of design capacity";
            if (b.fullWh.isMeasured() && b.designWh.isMeasured()) {
                v += " (" + oneDecimal(*b.fullWh.value) + " / " + oneDecimal(*b.designWh.value) + " Wh)";
            }
            rows.emplace_back("Health", v);
        } else {
            rows.emplace_back("Health", format(b.healthPct));
        }
        rows.emplace_back("Charge", format(b.chargePct));
        rows.emplace_back("Cycle count", format(b.cycleCount));
        rows.emplace_back("Voltage", format(b.voltageV));
        rows.emplace_back("Temperature", format(b.temperatureC));
        // A charging battery's rate is the charge rate, never consumption.
        if (b.rateW.isMeasured() && (b.status == "Charging" || b.status == "Discharging")) {
            rows.emplace_back("Rate", b.status + " at " + oneDecimal(*b.rateW.value) + " W");
        } else {
            rows.emplace_back("Rate", b.status.empty() ? "Unavailable" : b.status);
        }
    }
    return m;
}

}  // namespace pmp::vm
