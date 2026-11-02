// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/thermal_provider.hpp>

#include <algorithm>
#include <set>

namespace pmp::linux {
namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::set<std::string> listDir(const std::filesystem::path& dir) {
    std::set<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) out.insert(e.path().filename().string());
    return out;
}

struct HotspotCandidate {
    Metric value;
    std::optional<double> maxC, critC;
};

ThermalPressure pressureFor(double c, std::optional<double> maxC, std::optional<double> critC) {
    if (!maxC && !critC) return ThermalPressure::Unknown;
    const double crit = critC.value_or(*maxC);
    const double max = maxC.value_or(crit);
    if (c >= crit) return ThermalPressure::Critical;
    if (c >= max) return ThermalPressure::Heavy;
    if (c >= max - 10.0) return ThermalPressure::Moderate;
    return ThermalPressure::Nominal;
}

std::optional<double> threshold(const std::filesystem::path& p) {
    const auto n = readNumber(p);
    return n.value ? std::optional<double>(*n.value / 1000.0) : std::nullopt;
}

}  // namespace

void ThermalProvider::sample(Frame& out, const SampleContext&) {
    auto& t = out.thermals;
    t = {};
    t.hotspot = Metric::notMeasured(Availability::Unsupported, Unit::Celsius);

    std::optional<HotspotCandidate> best;
    int bestRank = 99;  // 0 coretemp package, 1 k10temp, 2 other CPU/SoC chips, then the x86_pkg_temp zone

    for (const auto& hw : listDir(root_.sys / "class/hwmon")) {
        const auto dir = root_.sys / "class/hwmon" / hw;
        const std::string name = trimmed(readText(dir / "name").text);
        for (const auto& file : listDir(dir)) {
            const auto under = file.find('_');
            if (under == std::string::npos || file.substr(under) != "_input") continue;
            const std::string stem = file.substr(0, under);  // temp1, fan1
            const std::string id = "hwmon:" + hw + ":" + stem;  // the directory, not the chip name: two NVMe drives share a name

            if (stem.rfind("temp", 0) == 0) {
                std::string label = trimmed(readText(dir / (stem + "_label")).text);
                SensorReading s;
                s.id = id;
                s.label = name + " " + (label.empty() ? stem : label);
                s.celsius = readMetric(dir / file, Unit::Celsius, 0.001);
                if (s.celsius.availability == Availability::Unsupported) continue;  // vanished between listing and read
                t.sensors.push_back(s);

                const bool socSensor = name == "cpu_thermal" || name == "cpu-thermal" || name == "soc_thermal" || name == "zenpower" ||
                                       name == "acpitz";  // boards and laptops without coretemp/k10temp
                const int rank = (name == "coretemp" && label.rfind("Package id", 0) == 0) ? 0
                                 : (name == "k10temp" && (label == "Tctl" || label == "Tdie")) ? 1
                                 : socSensor                                                      ? 2
                                                                                                  : 99;
                // A tie goes to the hotter measured reading; a reading that could not be read never beats one that could.
                const bool betterTie = rank == bestRank && s.celsius.isMeasured() &&
                                       (!best || !best->value.isMeasured() || *s.celsius.value > *best->value.value);
                if (rank < bestRank || betterTie) {
                    bestRank = rank;
                    best = HotspotCandidate{s.celsius, threshold(dir / (stem + "_max")), threshold(dir / (stem + "_crit"))};
                }
            } else if (stem.rfind("fan", 0) == 0) {
                FanReading fan;
                fan.id = id;
                fan.label = name + " " + stem;
                fan.rpm = readMetric(dir / file, Unit::Rpm);
                if (fan.rpm.availability != Availability::Unsupported) t.fans.push_back(fan);
            }
        }
    }

    if (bestRank == 99) best.reset();  // only a recognised CPU sensor counts as the hotspot
    if (!best) {
        for (const auto& z : listDir(root_.sys / "class/thermal")) {
            if (z.rfind("thermal_zone", 0) != 0) continue;
            const auto dir = root_.sys / "class/thermal" / z;
            if (trimmed(readText(dir / "type").text) != "x86_pkg_temp") continue;
            HotspotCandidate c;
            c.value = readMetric(dir / "temp", Unit::Celsius, 0.001);
            for (int i = 0; i < 10; ++i) {  // trip points give the thresholds when present
                const auto type = trimmed(readText(dir / ("trip_point_" + std::to_string(i) + "_type")).text);
                const auto temp = threshold(dir / ("trip_point_" + std::to_string(i) + "_temp"));
                if (type == "critical") c.critC = temp;
                if (type == "passive") c.maxC = temp;
            }
            best = c;
            break;
        }
    }
    if (best) {
        t.hotspot = best->value;
        if (best->value.isMeasured()) t.pressure = pressureFor(*best->value.value, best->maxC, best->critC);
    }
}

void ThermalProvider::fillUnavailable(Frame& out, Availability why) const {
    out.thermals = {};
    out.thermals.hotspot = Metric::notMeasured(why, Unit::Celsius);
}

}  // namespace pmp::linux
