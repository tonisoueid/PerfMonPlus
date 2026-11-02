// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/identity.hpp>
#include <perfmonplus/linux/process_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace pmp;
using namespace pmp::linux;

TEST_CASE("a real child is Alive, then Exited after it is killed", "[live][identity]") {
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        for (;;) pause();
    }

    const auto root = ProcRoot::live();
    const auto stat = readText(root.proc / std::to_string(child) / "stat");
    REQUIRE(stat.ok());
    const auto parsed = parseProcStat(stat.text);
    REQUIRE(parsed);
    const ProcessKey key{static_cast<std::int32_t>(child), parsed->startTicks};
    REQUIRE(verifyIdentity(root, key) == IdentityCheck::Alive);

    kill(child, SIGKILL);
    int status = 0;
    waitpid(child, &status, 0);
    REQUIRE(verifyIdentity(root, key) == IdentityCheck::Exited);
}
