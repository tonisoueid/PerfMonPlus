// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/trace/player.hpp>

#include "golden.hpp"

#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <time.h>

using namespace pmp;
using namespace pmp::trace;
using namespace pmp::testing;
using namespace std::chrono_literals;

namespace {

std::filesystem::path goldenPath() { return std::filesystem::path(PMP_TEST_DATA_DIR) / "golden-1.pmptrace"; }

}  // namespace

TEST_CASE("the golden trace opens and replays", "[trace][golden]") {
    Player p;
    const auto r = p.open(goldenPath());
    REQUIRE(r.error.empty());
    REQUIRE_FALSE(r.recovered);
    REQUIRE(p.sampleCount() == 20);
    REQUIRE(p.info().host.os == "macOS 15");  // made "elsewhere": nothing here is Linux-specific
    REQUIRE(p.info().host.cpuModel == "Apple M-class (fabricated)");
    REQUIRE(p.info().app == "PerfMonPlus golden-1");
    for (std::size_t i = 0; i < 20; ++i) REQUIRE(p.frame(i).has_value());
    const auto f7 = p.frame(7);
    REQUIRE(*f7->cpu.total.value == 27.5);  // documented in tests/data/README.md
    REQUIRE(f7->cpu.logical.size() == 4);
    REQUIRE(f7->cpu.logical[2].coreClass == CoreClass::Efficiency);
    REQUIRE(f7->disks.combined.writeBps.availability == Availability::Measured);  // the measured zero survives
    REQUIRE(*f7->disks.combined.writeBps.value == 0.0);
    REQUIRE(f7->net.combined.txBps.availability == Availability::Restricted);
    REQUIRE(f7->processes.processes.size() == 5);
    REQUIRE(f7->processes.processes[0].name == "compiler");
    REQUIRE(p.gapBefore(12));
    REQUIRE(p.wallAt(12) - p.wallAt(11) == 41s);
    REQUIRE_FALSE(p.gapBefore(11));
}

TEST_CASE("the golden file regenerates byte for byte", "[trace][golden]") {
    // Nothing about the machine may leak into the bytes: not the time zone, the locale, or the byte order.
    setenv("TZ", "Pacific/Kiritimati", 1);
    tzset();
    std::setlocale(LC_ALL, "de_DE.UTF-8");
    const auto fresh = goldenTrace();
    if (const char* out = std::getenv("PMP_WRITE_GOLDEN")) {  // how the checked-in file is (re)generated
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(fresh.data()), static_cast<std::streamsize>(fresh.size()));
        SUCCEED("wrote " << out);
        return;
    }
    std::ifstream in(goldenPath(), std::ios::binary);
    REQUIRE(in.good());
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(raw.size() == fresh.size());
    REQUIRE(std::memcmp(raw.data(), fresh.data(), raw.size()) == 0);
    std::setlocale(LC_ALL, "C");
    unsetenv("TZ");
    tzset();
}
