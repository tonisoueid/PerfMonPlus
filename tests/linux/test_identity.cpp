// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/identity.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

using pmp::ProcessKey;
using pmp::linux::IdentityCheck;
using pmp::linux::verifyIdentity;

namespace {
std::string statWithStart(int start) {
    return "42 (x) S 1 42 42 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 " + std::to_string(start) + " 1000 10 0";
}
}  // namespace

TEST_CASE("identity is Alive, Exited or Reused", "[identity]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statWithStart(5000));
    REQUIRE(verifyIdentity(fr.root(), ProcessKey{42, 5000}) == IdentityCheck::Alive);

    fr.write("proc/42/stat", statWithStart(9000));
    REQUIRE(verifyIdentity(fr.root(), ProcessKey{42, 5000}) == IdentityCheck::Reused);

    fr.remove("proc/42");
    REQUIRE(verifyIdentity(fr.root(), ProcessKey{42, 5000}) == IdentityCheck::Exited);
}
