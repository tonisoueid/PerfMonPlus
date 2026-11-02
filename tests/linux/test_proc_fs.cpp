// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/proc_fs.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>

using namespace pmp::linux;

TEST_CASE("parseColonValues reads numeric key/value lines", "[procfs]") {
    auto m = parseColonValues("MemTotal:  16384000 kB\nSwapFree:  0 kB\nName:\tfoo\n");
    REQUIRE(m.size() == 2);
    REQUIRE(m["MemTotal"] == 16384000);
    REQUIRE(m["SwapFree"] == 0);
}

TEST_CASE("parseSpaceValues reads key value lines", "[procfs]") {
    auto m = parseSpaceValues("pgpgin 7\npgpgout 9\n");
    REQUIRE(m["pgpgin"] == 7);
    REQUIRE(m["pgpgout"] == 9);
}

TEST_CASE("parseCpuList expands ranges", "[procfs]") {
    REQUIRE(parseCpuList("0-3,8\n") == std::vector<int>{0, 1, 2, 3, 8});
    REQUIRE(parseCpuList("\n").empty());
}

TEST_CASE("readText reports errno and availabilityForErrno maps it", "[procfs]") {
    FakeRoot fr;
    auto missing = readText(fr.root().proc / "nope");
    REQUIRE_FALSE(missing.ok());
    REQUIRE(missing.error == ENOENT);
    REQUIRE(pmp::linux::availabilityForErrno(ENOENT) == pmp::Availability::Unsupported);
    REQUIRE(pmp::linux::availabilityForErrno(EACCES) == pmp::Availability::Restricted);
    REQUIRE(pmp::linux::availabilityForErrno(EIO) == pmp::Availability::Failed);
}

TEST_CASE("sanitizeText makes foreign text safe", "[proc_fs]") {
    using pmp::linux::sanitizeText;
    REQUIRE(sanitizeText(std::string_view("a\0b\0", 4), 100) == "a b");
    REQUIRE(sanitizeText("tab\there\nnew", 100) == "tab?here?new");
    REQUIRE(sanitizeText("bad \xff\xfe bytes", 100) == "bad ?? bytes");
    REQUIRE(sanitizeText("caf\xc3\xa9", 100) == "caf\xc3\xa9");           // valid UTF-8 is kept
    REQUIRE(sanitizeText("trunc\xc3", 100) == "trunc?");                 // a cut multibyte sequence is invalid
    REQUIRE(sanitizeText(std::string(20000, 'x'), 8192).size() == 8192 + 3);  // limit plus the ellipsis
    REQUIRE(sanitizeText("<b>x</b>", 100) == "<b>x</b>");                // markup is data, not escaped here
}

TEST_CASE("a feature the kernel was built or booted without is Unsupported, not a fault", "[proc_fs][minor]") {
    CHECK(pmp::linux::availabilityForErrno(EOPNOTSUPP) == pmp::Availability::Unsupported);  // /proc/pressure/* with psi=0
    CHECK(pmp::linux::availabilityForErrno(ENOSYS) == pmp::Availability::Unsupported);
    CHECK(pmp::linux::availabilityForErrno(EIO) == pmp::Availability::Failed);
}
