// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/command.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace pmp::linux;
using namespace std::chrono_literals;

TEST_CASE("a command runs from argv without a shell", "[command]") {
    const auto r = runCommand({"printf", "%s", "a b;echo pwned"});
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.out == "a b;echo pwned");
}

TEST_CASE("stderr and exit codes are captured", "[command]") {
    const auto r = runCommand({"sh", "-c", "echo oops >&2; exit 3"});
    REQUIRE(r.exitCode == 3);
    REQUIRE(r.err == "oops\n");
}

TEST_CASE("a missing program is a failed result, not an exception", "[command]") {
    const auto r = runCommand({"no-such-binary-pmp"});
    REQUIRE(r.exitCode != 0);
    REQUIRE_FALSE(r.err.empty());
}

TEST_CASE("a command that hangs is killed at the timeout", "[command]") {
    const auto start = std::chrono::steady_clock::now();
    const auto r = runCommand({"sleep", "5"}, 200ms);
    REQUIRE(r.timedOut);
    REQUIRE(std::chrono::steady_clock::now() - start < 2s);
}

TEST_CASE("stdin is closed so a prompting command cannot block", "[command]") {
    const auto r = runCommand({"cat"}, 2s);
    REQUIRE_FALSE(r.timedOut);
    REQUIRE(r.exitCode == 0);
}

TEST_CASE("privileged actions wait for authorisation", "[command]") {
    REQUIRE(commandTimeout({"systemctl", "start", "--", "x.service"}) >= 1min);
    REQUIRE(commandTimeout({"systemctl", "--user", "disable", "--", "x.service"}) >= 1min);
    REQUIRE(commandTimeout({"loginctl", "terminate-session", "--", "3"}) >= 1min);
    REQUIRE(commandTimeout({"loginctl", "lock-session", "--", "3"}) >= 1min);
    REQUIRE(commandTimeout({"systemctl", "--user", "list-unit-files", "--state=enabled,disabled"}) == 10s);
    REQUIRE(commandTimeout({"journalctl", "--unit=start", "-n", "5"}) == 10s);  // "start" inside an option is not a verb
    REQUIRE(commandTimeout({"systemctl", "list-units", "--", "start"}) == 10s);  // after "--" it is a unit name
}

TEST_CASE("a failed command is described honestly", "[command]") {
    CommandResult timedOut;
    timedOut.timedOut = true;
    REQUIRE(describeFailure(timedOut).find("timed out") != std::string::npos);
    CommandResult denied;
    denied.exitCode = 1;
    denied.err = "Access denied\n";
    REQUIRE(describeFailure(denied) == "Access denied");
    REQUIRE(describeFailure(CommandResult{}) == "the command failed");
}
