// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/services.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <stdexcept>

using namespace pmp::linux;

namespace {

constexpr const char* kUnits =
    R"([{"unit":"accounts-daemon.service","load":"loaded","active":"active","sub":"running","description":"Accounts Service"},)"
    R"({"unit":"alsa-restore.service","load":"loaded","active":"active","sub":"exited","description":"Save/Restore Sound Card State"}])";
constexpr const char* kFiles =
    R"([{"unit_file":"accounts-daemon.service","state":"enabled","preset":"enabled"},)"
    R"({"unit_file":"alsa-restore.service","state":"static","preset":null},)"
    R"({"unit_file":"alsa-card-wait@.service","state":"static","preset":null}])";
constexpr const char* kShow = "Id=accounts-daemon.service\nMainPID=812\n\nId=alsa-restore.service\nMainPID=0\n";

struct Fake {
    std::vector<std::vector<std::string>> calls;
    int failActionWith = 0;
    std::string failListWith;
    CommandRunner runner() {
        return [this](const std::vector<std::string>& argv) {
            calls.push_back(argv);
            CommandResult r;
            r.exitCode = 0;
            const auto has = [&](const char* w) { return std::find(argv.begin(), argv.end(), w) != argv.end(); };
            if (has("list-units")) {
                if (!failListWith.empty()) return CommandResult{1, "", failListWith, false};
                r.out = kUnits;
            } else if (has("list-unit-files")) r.out = kFiles;
            else if (has("show")) r.out = kShow;
            else if (failActionWith) return CommandResult{failActionWith, "", "Interactive authentication required.", false};
            return r;
        };
    }
};

}  // namespace

TEST_CASE("services join units with their unit files and main pids", "[services]") {
    Fake fake;
    ServicesCollector c(fake.runner());
    const auto s = c.collect(ServiceScope::System);
    REQUIRE(s.size() == 2);  // the template accounts for no service of its own
    REQUIRE(s[0].unit == "accounts-daemon.service");
    REQUIRE(s[0].startType == "enabled");
    REQUIRE(s[0].mainPid == 812);
    REQUIRE(s[0].sub == "running");
    REQUIRE(s[0].description == "Accounts Service");
    REQUIRE(s[1].startType == "static");
    REQUIRE(s[1].mainPid == 0);
}

TEST_CASE("user scope adds --user", "[services]") {
    Fake fake;
    ServicesCollector c(fake.runner());
    c.collect(ServiceScope::User);
    for (const auto& call : fake.calls) REQUIRE(call[1] == "--user");
}

TEST_CASE("unit names follow a double dash", "[services]") {
    Fake fake;
    ServicesCollector c(fake.runner());
    c.act(ServiceScope::System, "restart", "-foo.service");
    REQUIRE(fake.calls.back() == std::vector<std::string>{"systemctl", "restart", "--", "-foo.service"});
    c.act(ServiceScope::User, "stop", "app-gnome\\x2dkeyring@autostart.service");
    REQUIRE(fake.calls.back() ==
            std::vector<std::string>{"systemctl", "--user", "stop", "--", "app-gnome\\x2dkeyring@autostart.service"});
}

TEST_CASE("only start, stop and restart are allowed", "[services]") {
    Fake fake;
    ServicesCollector c(fake.runner());
    REQUIRE_THROWS_AS(c.act(ServiceScope::System, "mask", "x.service"), std::invalid_argument);
    REQUIRE_THROWS_AS(c.act(ServiceScope::System, "restart; reboot", "x.service"), std::invalid_argument);
    REQUIRE(fake.calls.empty());
}

TEST_CASE("a failed listing throws with the stderr text", "[services]") {
    Fake fake;
    fake.failListWith = "Failed to connect to bus";
    ServicesCollector c(fake.runner());
    try {
        c.collect(ServiceScope::System);
        FAIL("expected an exception");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("Failed to connect to bus") != std::string::npos);
    }
}

TEST_CASE("logs ask journalctl for the unit", "[services]") {
    Fake fake;
    ServicesCollector c(fake.runner());
    c.logs(ServiceScope::System, "x.service", 50);
    const auto& argv = fake.calls.back();
    REQUIRE(argv[0] == "journalctl");
    REQUIRE(std::find(argv.begin(), argv.end(), "--unit=x.service") != argv.end());
    REQUIRE(std::find(argv.begin(), argv.end(), "50") != argv.end());
}

TEST_CASE("no active units means no main-pid query, in either scope", "[services][minor]") {
    for (auto scope : {ServiceScope::System, ServiceScope::User}) {
        std::vector<std::vector<std::string>> calls;
        ServicesCollector c([&](const std::vector<std::string>& argv) {
            calls.push_back(argv);
            CommandResult r;
            r.exitCode = 0;
            const auto has = [&](const char* w) { return std::find(argv.begin(), argv.end(), w) != argv.end(); };
            if (has("list-units")) r.out = R"([{"unit":"a.service","load":"loaded","active":"inactive","sub":"dead","description":"A"}])";
            else if (has("list-unit-files")) r.out = R"([{"unit_file":"a.service","state":"enabled","preset":null}])";
            return r;
        });
        c.collect(scope);
        for (const auto& call : calls) REQUIRE(std::find(call.begin(), call.end(), "show") == call.end());
    }
}

TEST_CASE("the journal's permission hint is kept even when journalctl succeeds", "[services][minor]") {
    ServicesCollector c([](const std::vector<std::string>&) {
        CommandResult r;
        r.exitCode = 0;
        r.out = "2026-10-25T10:00:00 host x[1]: started\n";
        r.err = "Hint: You are currently not seeing messages from other users and the system.\n      Users in groups 'adm', 'systemd-journal' can see all messages.\n";
        return r;
    });
    const auto text = c.logs(ServiceScope::System, "x.service", 50);
    REQUIRE(text.find("started") != std::string::npos);
    REQUIRE(text.find("Users in groups 'adm', 'systemd-journal'") != std::string::npos);  // otherwise nobody learns why it is thin
    CommandResult clean;
    clean.exitCode = 0;
    clean.out = "line\n";
    ServicesCollector c2([clean](const std::vector<std::string>&) { return clean; });
    REQUIRE(c2.logs(ServiceScope::System, "x.service", 50) == "line\n");  // no stderr: unchanged
}
