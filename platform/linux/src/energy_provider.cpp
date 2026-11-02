// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/energy_provider.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <set>

namespace pmp::linux {
namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string prettify(std::string s) {  // balance_power -> "Balance power"
    for (auto& c : s) {
        if (c == '_' || c == '-') c = ' ';
    }
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::set<std::string> listDir(const std::filesystem::path& dir) {
    std::set<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) out.insert(e.path().filename().string());
    return out;
}

Metric unsupported(Unit u) { return Metric::notMeasured(Availability::Unsupported, u); }

BatteryInfo readBattery(const std::filesystem::path& dir) {
    BatteryInfo b;
    b.present = true;
    b.status = trimmed(readText(dir / "status").text);
    if (b.status.empty()) b.status = "Unknown";
    b.chargePct = readMetric(dir / "capacity", Unit::Percent);
    b.cycleCount = readMetric(dir / "cycle_count", Unit::Count);

    // Energy (µWh) based batteries; charge (µAh) based ones use the same ratios.
    const bool energy = readNumber(dir / "energy_now").value.has_value();
    const char* now = energy ? "energy_now" : "charge_now";
    const char* full = energy ? "energy_full" : "charge_full";
    const char* design = energy ? "energy_full_design" : "charge_full_design";
    const auto nowV = readNumber(dir / now).value;
    const auto fullV = readNumber(dir / full).value;
    const auto designV = readNumber(dir / design).value;
    b.voltageV = readNumber(dir / "voltage_now").value
                     ? Metric::measured(*readNumber(dir / "voltage_now").value / 1e6, Unit::Volts)
                     : unsupported(Unit::Volts);
    b.temperatureC = readNumber(dir / "temp").value ? Metric::measured(*readNumber(dir / "temp").value / 10.0, Unit::Celsius)
                                                    : unsupported(Unit::Celsius);
    // Wh: energy-based batteries report µWh; charge-based ones report µAh, which needs a voltage to become Wh.
    const auto volts = readNumber(dir / "voltage_min_design").value ? readNumber(dir / "voltage_min_design").value
                                                                     : readNumber(dir / "voltage_now").value;
    auto wh = [&](std::optional<double> v) {
        if (!v) return unsupported(Unit::WattHours);
        if (energy) return Metric::measured(*v / 1e6, Unit::WattHours);
        if (volts) return Metric::measured(*v * *volts / 1e12, Unit::WattHours);
        return unsupported(Unit::WattHours);
    };
    b.designWh = wh(designV);
    b.fullWh = wh(fullV);
    b.currentWh = wh(nowV);
    b.healthPct = fullV && designV && *designV > 0 ? Metric::measured(*fullV / *designV * 100.0, Unit::Percent)
                                                   : unsupported(Unit::Percent);

    std::optional<double> rateUnits;  // µW (energy) or µA (charge): the same unit family as `now`
    b.rateW = unsupported(Unit::Watts);
    if (const auto p = readNumber(dir / "power_now").value) {
        rateUnits = *p;
        b.rateW = Metric::measured(*p / 1e6, Unit::Watts);
    } else if (const auto i = readNumber(dir / "current_now").value) {
        const auto v = readNumber(dir / "voltage_now").value;
        if (v) b.rateW = Metric::measured(*i * *v / 1e12, Unit::Watts);
        // The remaining-time maths divides the charge (or energy) by the rate in the same family: µAh by µA, but µWh
        // needs µW, so an energy-based battery that only reports current gets its power from current x voltage.
        if (!energy) rateUnits = *i;
        else if (v) rateUnits = *i * *v / 1e6;
    }

    b.timeRemaining = unsupported(Unit::Seconds);
    if (nowV && fullV && rateUnits && *rateUnits > 0) {
        if (b.status == "Discharging") {
            b.timeRemaining = Metric::measured(*nowV / *rateUnits * 3600.0, Unit::Seconds);
        } else if (b.status == "Charging") {
            b.timeRemaining = Metric::measured((*fullV - *nowV) / *rateUnits * 3600.0, Unit::Seconds);
            b.timeIsToFull = true;
        }
    }
    return b;
}

std::string raplLabel(const std::string& name) {
    if (name.starts_with("package")) return "Package";
    if (name == "core") return "CPU cores";
    if (name == "uncore") return "Uncore / iGPU";
    if (name == "dram") return "Memory (DRAM)";
    if (name == "psys") return "Platform";
    return prettify(name);
}

}  // namespace

void EnergyProvider::readDomains(EnergySnapshot& e, const SampleContext& ctx) {
    const auto root = root_.sys / "class/powercap";
    if (!domainsListed_ || ctx.generation >= domainsListedAt_ + 30) {  // a hot-plugged domain appears eventually
        domainDirs_.clear();
        std::set<std::string> used;
        for (const auto& name : listDir(root)) {
            if (!name.starts_with("intel-rapl:")) continue;
            std::string id = trimmed(readText(root / name / "name").text);
            if (id.empty()) id = name.substr(std::string("intel-rapl:").size());
            if (!used.insert(id).second) id += "@" + name.substr(std::string("intel-rapl:").size());  // a second socket's "core"
            domainDirs_.push_back({root / name, id, raplLabel(id.substr(0, id.find('@')))});
            domainDirs_.back().socket = std::atoi(name.c_str() + std::string("intel-rapl:").size());  // "intel-rapl:1:0" is socket 1
        }
        // Two sockets give two "Package" and two "CPU cores": say which is which, but only where they would collide.
        std::map<std::string, int> seen;
        for (const auto& d : domainDirs_) ++seen[d.label];
        for (auto& d : domainDirs_) {
            if (seen[d.label] > 1) d.label += " (socket " + std::to_string(d.socket) + ")";
        }
        domainsListed_ = true;
        domainsListedAt_ = ctx.generation;
    }
    for (const auto& d : domainDirs_) {
        PowerDomain pd;
        pd.id = d.id;
        pd.label = d.label;
        const auto uj = readNumber(d.dir / "energy_uj");
        if (!uj.value) {
            pd.watts = Metric::notMeasured(availabilityForErrno(uj.error), Unit::Watts);
            domainPrev_.erase(d.id);
        } else {
            pd.watts = Metric::notMeasured(Availability::Pending, Unit::Watts);
            const auto it = domainPrev_.find(d.id);
            if (it != domainPrev_.end()) {
                const double dt = std::chrono::duration<double>(ctx.mono - it->second.t).count();
                double delta = *uj.value - it->second.uj;
                if (delta < 0) delta += readNumber(d.dir / "max_energy_range_uj").value.value_or(0.0);
                if (dt > 0 && delta >= 0) pd.watts = Metric::measured(delta / 1e6 / dt, Unit::Watts);
            }
            domainPrev_[d.id] = {*uj.value, ctx.mono};
        }
        e.domains.push_back(std::move(pd));
    }
}

void EnergyProvider::sample(Frame& out, const SampleContext& ctx) {
    auto& e = out.energy;
    e = {};
    e.watts = unsupported(Unit::Watts);

    bool acOnline = false, haveBattery = false;
    for (const auto& name : listDir(root_.sys / "class/power_supply")) {
        const auto dir = root_.sys / "class/power_supply" / name;
        const auto type = trimmed(readText(dir / "type").text);
        if (type == "Mains" && trimmed(readText(dir / "online").text) == "1") acOnline = true;
        if (type == "Battery" && !haveBattery) {
            e.battery = readBattery(dir);
            haveBattery = true;
        }
    }
    readDomains(e, ctx);
    e.source = acOnline ? PowerSource::AC : haveBattery ? PowerSource::Battery : PowerSource::Unknown;

    // Watts: RAPL package power when readable, else battery discharge. A charging battery's rate is the charge
    // rate, never consumption.
    const auto raplDir = root_.sys / "class/powercap/intel-rapl:0";
    const auto rapl = readNumber(raplDir / "energy_uj");
    bool restricted = false;
    if (rapl.value) {
        e.wattsSource = WattsSource::CpuPackage;
        e.watts = Metric::notMeasured(Availability::Pending, Unit::Watts);
        if (prevUj_ && prevTime_) {
            const double dt = std::chrono::duration<double>(ctx.mono - *prevTime_).count();
            double delta = *rapl.value - *prevUj_;
            if (delta < 0) delta += readNumber(raplDir / "max_energy_range_uj").value.value_or(0.0);  // counter wrapped
            if (dt > 0 && delta >= 0) e.watts = Metric::measured(delta / 1e6 / dt, Unit::Watts);
        }
        prevUj_ = *rapl.value;
        prevTime_ = ctx.mono;
    } else {
        prevUj_.reset();
        restricted = availabilityForErrno(rapl.error) == Availability::Restricted;
        if (e.battery.present && e.battery.status == "Discharging" && e.battery.rateW.isMeasured()) {
            e.watts = e.battery.rateW;
            e.wattsSource = WattsSource::BatteryDischarge;
        } else if (restricted) {
            e.watts = Metric::notMeasured(Availability::Restricted, Unit::Watts);
            e.restrictedPath = (raplDir / "energy_uj").string();
        } else if (rapl.error != 0 && rapl.error != ENOENT && rapl.error != ENOTDIR) {
            e.watts = Metric::notMeasured(availabilityForErrno(rapl.error), Unit::Watts);  // there, but unreadable or garbled: a fault
        }
    }

    std::string mode = trimmed(readText(root_.sys / "firmware/acpi/platform_profile").text);
    if (mode.empty()) mode = trimmed(readText(root_.sys / "devices/system/cpu/cpu0/cpufreq/energy_performance_preference").text);
    e.powerMode = prettify(mode);
}

void EnergyProvider::fillUnavailable(Frame& out, Availability why) const {
    out.energy = {};
    out.energy.watts = Metric::notMeasured(why, Unit::Watts);
}

}  // namespace pmp::linux
