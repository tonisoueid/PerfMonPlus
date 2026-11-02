// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/version_compare.hpp>

#include <catch2/catch_test_macros.hpp>

using pmp::vm::compareVersions;

TEST_CASE("versions compare numerically", "[version]") {
    REQUIRE(compareVersions("0.1.0", "0.2.0") < 0);
    REQUIRE(compareVersions("1.2.10", "1.2.9") > 0);
    REQUIRE(compareVersions("1.0", "1.0.0") == 0);
    REQUIRE(compareVersions("2.0.0", "1.99.99") > 0);
}

TEST_CASE("non-numeric version parts compare as zero", "[version]") {
    REQUIRE(compareVersions("1.x.0", "1.0.0") == 0);
    REQUIRE(compareVersions("", "0.0.1") < 0);
    REQUIRE(compareVersions("1.2.3-beta", "1.2.3") == 0);  // a pre-release suffix is ignored, never a reason to nag
}
