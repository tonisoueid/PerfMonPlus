// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifndef PMP_APP_PATH
#define PMP_APP_PATH "perfmonplus"
#endif

#ifdef PMP_OPTIMIZED
namespace {

// Runs the real app headless for `seconds` and returns its own CPU use in percent of one core. `worstAppendMs`, when
// given, receives the slowest recorded sample the app reports (--record).
double appCpuPercent(const std::string& extraFlags, int seconds, double* worstAppendMs = nullptr) {
    const std::string cmd = std::string("env HOME=/tmp/pmp-live-home QT_QPA_PLATFORM=offscreen ") + PMP_APP_PATH +
                            " --run-seconds " + std::to_string(seconds) + " " + extraFlags + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    REQUIRE(p != nullptr);
    char line[256];
    double value = -1;
    while (std::fgets(line, sizeof line, p)) {
        double v = -1;
        if (std::sscanf(line, "cpu_percent=%lf", &v) == 1) value = v;
        if (worstAppendMs && std::sscanf(line, "record_worst_ms=%lf", &v) == 1) *worstAppendMs = v;
    }
    pclose(p);
    return value;
}

}  // namespace
#endif

TEST_CASE("default settings stay under 2% total cpu", "[live][gui]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 2.0);
#endif
}

TEST_CASE("low-effects mode stays under 0.5% total cpu", "[live][gui]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--low-effects", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 0.5);
#endif
}

TEST_CASE("the Processes page stays under 2% total cpu", "[live][gui][processes]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--page processes", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 2.0);
#endif
}

TEST_CASE("the Processes page in low-effects mode stays under 0.5% total cpu", "[live][gui][processes]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--page processes --low-effects", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 0.5);
#endif
}

// ---- P4: the tools ----------------------------------------------------------------------------------------------

TEST_CASE("the Power & Freq page stays under 2% total cpu", "[live][gui][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--page power-freq", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 2.0);
#endif
}

TEST_CASE("the Power & Freq page in low-effects mode stays under 0.5% total cpu", "[live][gui][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--page power-freq --low-effects", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 0.5);
#endif
}

TEST_CASE("the Connections page stays under 2% total cpu", "[live][gui][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--page connections", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 2.0);
#endif
}

TEST_CASE("the Connections page in low-effects mode stays under 0.5% total cpu", "[live][gui][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const double onCore = appCpuPercent("--page connections --low-effects", 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 0.5);
#endif
}

TEST_CASE("an active recording stays under 2% total cpu and no sample takes 8 ms", "[live][gui][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    double worst = -1;
    const double onCore = appCpuPercent("--record", 60, &worst);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total << ", worst append " << worst << " ms");
    REQUIRE(onCore >= 0);
    REQUIRE(total < 2.0);
    REQUIRE(worst >= 0);
    REQUIRE(worst < 8.0);
#endif
}

TEST_CASE("a replay playing at 1x stays under 2% total cpu", "[live][gui][tools]") {
#ifndef PMP_OPTIMIZED
    SKIP("CPU budgets only apply to optimized builds (configure with -DCMAKE_BUILD_TYPE=Release)");
#else
    const std::string golden = std::string("--open-recording ") + PMP_TEST_DATA_DIR + "/golden-1.pmptrace --play";
    const double onCore = appCpuPercent(golden, 60);
    const double total = onCore / std::thread::hardware_concurrency();
    INFO("cpu_percent (of one core) = " << onCore << ", of the whole machine = " << total);
    REQUIRE(onCore >= 0);
    REQUIRE(total < 2.0);
#endif
}
