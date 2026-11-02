// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/vm/apps_model.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::linux;

namespace {

InstalledApp app(const std::string& id, AppSource src, double bytes, const std::string& publisher = "Pub") {
    InstalledApp a;
    a.id = a.name = id;
    a.source = src;
    a.publisher = publisher;
    a.version = "1.0";
    if (bytes >= 0) a.sizeBytes = Metric::measured(bytes, Unit::Bytes);
    return a;
}

}  // namespace

TEST_CASE("sorted by size descending by default", "[apps-vm]") {
    const auto rows = buildApps({app("small", AppSource::Dpkg, 10), app("big", AppSource::Dpkg, 5000), app("mid", AppSource::Snap, 300)}, {});
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].name == "big");
    REQUIRE(rows[1].name == "mid");
    REQUIRE(rows[2].name == "small");
}

TEST_CASE("unknown sizes sort last and display Unavailable", "[apps-vm]") {
    const std::vector<InstalledApp> apps = {app("nosize", AppSource::Dpkg, -1), app("a", AppSource::Dpkg, 100), app("zero", AppSource::Dpkg, 0)};
    auto rows = buildApps(apps, {});
    REQUIRE(rows[0].name == "a");
    REQUIRE(rows[1].name == "zero");
    REQUIRE(rows[1].sizeKnown);  // a measured zero is still a size
    REQUIRE(rows[2].name == "nosize");
    REQUIRE(rows[2].size == "Unavailable");
    rows = buildApps(apps, {.descending = false});
    REQUIRE(rows.back().name == "nosize");  // last in both directions
    REQUIRE(rows[0].name == "zero");
}

TEST_CASE("filter matches name, publisher and source", "[apps-vm]") {
    const std::vector<InstalledApp> apps = {app("firefox", AppSource::Snap, 1, "Mozilla"), app("bash", AppSource::Dpkg, 1, "GNU"), app("gimp", AppSource::Flatpak, 1, "flathub")};
    REQUIRE(buildApps(apps, {.filter = "MOZ"}).size() == 1);
    REQUIRE(buildApps(apps, {.filter = "snap"}).size() == 1);
    REQUIRE(buildApps(apps, {.filter = "bash"}).size() == 1);
    REQUIRE(buildApps(apps, {.filter = "xyz"}).empty());
    REQUIRE(buildApps(apps, {.source = AppSource::Flatpak}).size() == 1);
}

TEST_CASE("last used is never invented", "[apps-vm]") {
    const auto rows = buildApps({app("a", AppSource::Dpkg, 1)}, {});
    REQUIRE(rows[0].lastUsed == "Not tracked");
    REQUIRE(rows[0].installed == "—");
}

TEST_CASE("install dates print as ISO dates", "[apps-vm]") {
    auto a = app("a", AppSource::Dpkg, 1);
    a.installed = std::chrono::system_clock::time_point(std::chrono::seconds(1'700'000'000));
    REQUIRE(buildApps({a}, {})[0].installed == "2023-11-14");
}

TEST_CASE("summary counts only known sizes and reports the unknown ones", "[apps-vm]") {
    const auto s = summariseApps({app("a", AppSource::Dpkg, 2e9), app("b", AppSource::Snap, 1e9), app("c", AppSource::Dpkg, -1), app("d", AppSource::Dpkg, -1)});
    REQUIRE(s.unknownSize == 2);
    REQUIRE(s.note == "2 apps without a known size are not counted");
    REQUIRE(s.bySource.size() == 2);
    REQUIRE(s.bySource[0].first == AppSource::Dpkg);  // largest first
    REQUIRE(s.bySource[0].second == 2e9);
    REQUIRE(s.total.find("in 4 apps") != std::string::npos);
    REQUIRE(summariseApps({app("c", AppSource::Dpkg, -1)}).note == "1 app without a known size is not counted");
    REQUIRE(summariseApps({}).note.empty());
}

TEST_CASE("size text follows the unit setting", "[apps-vm]") {
    const auto before = formatOptions();
    setFormatOptions({.binaryBytes = false, .fahrenheit = false});
    const auto dec = buildApps({app("a", AppSource::Dpkg, 1'500'000)}, {});
    setFormatOptions({.binaryBytes = true, .fahrenheit = false});
    const auto bin = buildApps({app("a", AppSource::Dpkg, 1'500'000)}, {});
    setFormatOptions(before);
    REQUIRE(dec[0].size != bin[0].size);
}

TEST_CASE("kernel modules sort by size and show who uses them", "[apps-vm][modules]") {
    std::vector<KernelModule> m(3);
    m[0] = {"small", 100, 0, {}, "Live"};
    m[1] = {"big", 90000, 2, {"a", "b"}, "Live"};
    m[2] = {"mid", 5000, 1, {"big"}, "Loading"};
    auto rows = buildModules(m, "");
    REQUIRE(rows[0].name == "big");
    REQUIRE(rows[0].usedBy == "a, b");
    REQUIRE(rows[2].usedBy == "—");
    REQUIRE(buildModules(m, "big").size() == 2);  // the module itself and the one that uses it
    rows = buildModules(m, "", "name", false);
    REQUIRE(rows[0].name == "big");
    REQUIRE(rows[2].name == "small");
    rows = buildModules(m, "", "state", true);
    REQUIRE(rows[0].state == "Loading");
}
