// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/sessions.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp::linux;

namespace {
constexpr const char* kJson =
    R"([{"session":"1","uid":1000,"user":"tsoueid","seat":null,"leader":1824,"class":"manager","tty":null,"idle":false,"since":null},)"
    R"({"session":"3","uid":1000,"user":"tsoueid","seat":"seat0","leader":3750,"class":"user","tty":"tty2","idle":false,"since":null}])";
}

TEST_CASE("loginctl json becomes sessions", "[sessions]") {
    std::vector<std::string> seen;
    SessionsCollector c([&](const std::vector<std::string>& argv) {
        seen = argv;
        return CommandResult{0, kJson, "", false};
    });
    const auto s = c.collect();
    REQUIRE(seen == std::vector<std::string>{"loginctl", "--json=short", "list-sessions"});
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].id == "1");
    REQUIRE(s[0].seat.empty());
    REQUIRE(s[0].tty.empty());
    REQUIRE(s[1].seat == "seat0");
    REQUIRE(s[1].tty == "tty2");
    REQUIRE(s[1].user == "tsoueid");
    REQUIRE(s[1].uid == 1000);
    REQUIRE(s[1].leader == 3750);
    REQUIRE(s[1].sessionClass == "user");
}

TEST_CASE("session actions pass the id after a double dash", "[sessions]") {
    std::vector<std::string> seen;
    SessionsCollector c([&](const std::vector<std::string>& argv) {
        seen = argv;
        return CommandResult{0, "", "", false};
    });
    c.terminate("3");
    REQUIRE(seen == std::vector<std::string>{"loginctl", "terminate-session", "--", "3"});
    c.lock("-1");
    REQUIRE(seen == std::vector<std::string>{"loginctl", "lock-session", "--", "-1"});
}

TEST_CASE("a failing loginctl throws with its message", "[sessions]") {
    SessionsCollector c([](const std::vector<std::string>&) { return CommandResult{1, "", "no logind", false}; });
    REQUIRE_THROWS_AS(c.collect(), std::runtime_error);
}
