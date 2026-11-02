// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/tables_model.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using pmp::linux::ServiceInfo;

namespace {

ServiceInfo svc(const char* unit, const char* desc, const char* active, const char* sub, int pid = 0) {
    ServiceInfo s;
    s.unit = unit;
    s.description = desc;
    s.active = active;
    s.sub = sub;
    s.startType = "enabled";
    s.mainPid = pid;
    return s;
}

std::vector<ServiceInfo> sample() {
    return {svc("accounts-daemon.service", "Accounts Service", "active", "running", 812),
            svc("alsa-restore.service", "Save/Restore Sound Card State", "active", "exited"),
            svc("broken.service", "A failed thing", "failed", "failed"),
            svc("idle.service", "Not running", "inactive", "dead")};
}

}  // namespace

TEST_CASE("service rows show friendly status and hide a zero pid", "[tables]") {
    const auto rows = serviceRows(sample(), "", StatusFilter::All);
    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0].status == "Running");
    REQUIRE(rows[0].pid == "812");
    REQUIRE(rows[1].status == "Exited");
    REQUIRE(rows[1].pid.empty());
    REQUIRE(rows[2].status == "Failed");
    REQUIRE(rows[3].status == "Stopped");
}

TEST_CASE("service filter matches unit or description, case-insensitively", "[tables]") {
    REQUIRE(serviceRows(sample(), "ALSA", StatusFilter::All).size() == 1);
    REQUIRE(serviceRows(sample(), "sound card", StatusFilter::All).size() == 1);
    REQUIRE(serviceRows(sample(), "nothing like this", StatusFilter::All).empty());
}

TEST_CASE("status filters", "[tables]") {
    REQUIRE(serviceRows(sample(), "", StatusFilter::Failed).size() == 1);
    REQUIRE(serviceRows(sample(), "", StatusFilter::Stopped).size() == 1);
    REQUIRE(serviceRows(sample(), "", StatusFilter::Running).size() == 1);
}

#include "support/frame_fixtures.hpp"

TEST_CASE("startup rows describe where each entry comes from", "[tables]") {
    std::vector<linux::StartupEntry> entries(3);
    entries[0].name = "Geoclue Demo agent";
    entries[0].source = linux::StartupSource::XdgSystem;
    entries[0].command = "/usr/libexec/agent";
    entries[1].name = "Mine";
    entries[1].source = linux::StartupSource::XdgUser;
    entries[1].enabled = false;
    entries[2].name = "syncthing.service";
    entries[2].source = linux::StartupSource::SystemdUser;
    const auto rows = startupRows(entries);
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].status == "Enabled");
    REQUIRE(rows[0].source == "Autostart (system)");
    REQUIRE(rows[0].impact == "Not measured");
    REQUIRE(rows[0].command == "/usr/libexec/agent");
    REQUIRE(rows[1].status == "Disabled");
    REQUIRE(rows[1].source == "Autostart (you)");
    REQUIRE(rows[2].source == "systemd user unit");
}

namespace {
linux::SessionInfo session(const char* id, const char* user, const char* seat, const char* tty, const char* cls) {
    linux::SessionInfo s;
    s.id = id;
    s.user = user;
    s.seat = seat;
    s.tty = tty;
    s.sessionClass = cls;
    return s;
}
}  // namespace

TEST_CASE("user rows aggregate a user's processes and skip manager sessions", "[tables]") {
    Frame f;
    for (auto [user, cpu, mem] : {std::tuple{"tsoueid", 10.0, 100.0}, {"tsoueid", 5.0, 50.0}, {"root", 3.0, 10.0}}) {
        ProcessRecord r;
        r.key = {static_cast<std::int32_t>(f.processes.processes.size() + 1), 1};
        r.user = user;
        r.cpu = Metric::measured(cpu, Unit::Percent);
        r.memFootprint = Metric::measured(mem * 1024 * 1024, Unit::Bytes);
        r.diskReadBps = Metric::measured(1024, Unit::BytesPerSec);
        r.diskWriteBps = Metric::measured(1024, Unit::BytesPerSec);
        f.processes.processes.push_back(r);
    }
    const auto rows = userRows({session("1", "tsoueid", "", "", "manager"), session("3", "tsoueid", "seat0", "tty2", "user")}, f);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].user == "tsoueid");
    REQUIRE(rows[0].sessions == "tty2, seat0");
    REQUIRE(rows[0].cpu == "15.0%");
    REQUIRE(rows[0].memory == "150.0 MB");
    REQUIRE(rows[0].diskPerSec == "4 KB/s");  // two processes, 1 KB/s read + 1 KB/s write each
    REQUIRE(rows[0].status == "Active");
    REQUIRE(rows[0].sessionIds == std::vector<std::string>{"3"});
}

TEST_CASE("unmeasured process data stays a dash for a user", "[tables]") {
    Frame f;
    ProcessRecord r;
    r.key = {1, 1};
    r.user = "tsoueid";  // cpu / memory stay Pending
    f.processes.processes.push_back(r);
    const auto rows = userRows({session("3", "tsoueid", "seat0", "tty2", "user")}, f);
    REQUIRE(rows[0].cpu == "—");
    REQUIRE(rows[0].memory == "—");
}

TEST_CASE("an idle session reads Idle", "[tables]") {
    auto s = session("3", "u", "seat0", "tty2", "user");
    s.idle = true;
    REQUIRE(userRows({s}, Frame{})[0].status == "Idle");
}
