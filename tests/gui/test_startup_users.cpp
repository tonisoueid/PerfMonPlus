// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/startup_page.hpp>
#include <perfmonplus/gui/users_page.hpp>

#include "support/fake_root.hpp"
#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <mutex>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::linux;

namespace {

struct Fake {
    std::mutex m;
    std::vector<std::vector<std::string>> calls;
    bool failSessionActions = false;
    CommandRunner runner() {
        return [this](const std::vector<std::string>& argv) {
            std::lock_guard lock(m);
            calls.push_back(argv);
            const auto has = [&](const char* w) { return std::find(argv.begin(), argv.end(), w) != argv.end(); };
            if (has("list-unit-files"))
                return CommandResult{0, R"([{"unit_file":"syncthing.service","state":"enabled"}])", "", false};
            if (has("list-sessions"))
                return CommandResult{0,
                                     R"([{"session":"3","uid":1000,"user":"tsoueid","seat":"seat0","leader":3750,"class":"user","tty":"tty2","idle":false}])",
                                     "", false};
            if (failSessionActions && (has("terminate-session") || has("lock-session")))
                return CommandResult{1, "", "Interactive authentication required.", false};
            return CommandResult{0, "", "", false};
        };
    }
    bool called(const std::vector<std::string>& argv) {
        std::lock_guard lock(m);
        return std::find(calls.begin(), calls.end(), argv) != calls.end();
    }
};

template <class Obj, class Signal>
bool waitFor(Obj* obj, Signal sig, int ms = 3000) {
    QEventLoop loop;
    bool fired = false;
    QObject::connect(obj, sig, &loop, [&] {
        fired = true;
        loop.quit();
    });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return fired;
}

XdgDirs emptyDirs(FakeRoot& fr) {
    XdgDirs d;
    d.userConfig = fr.root().proc.parent_path() / "home/.config";
    d.systemConfig = {fr.root().proc.parent_path() / "etc/xdg"};
    return d;
}

}  // namespace

TEST_CASE("toggling a startup entry calls the collector and refreshes", "[gui][startup]") {
    FakeRoot fr;
    Fake fake;
    StartupPage page(std::make_shared<StartupCollector>(emptyDirs(fr), fake.runner()));
    page.refresh();
    REQUIRE(waitFor(&page, &StartupPage::refreshed));
    REQUIRE(page.table()->rowCount() == 1);
    REQUIRE(page.table()->item(0, 0)->text() == "syncthing.service");
    REQUIRE(page.table()->item(0, 1)->text() == "Enabled");

    page.setEnabledAt(0, false);
    REQUIRE(waitFor(&page, &StartupPage::refreshed));
    REQUIRE(fake.called({"systemctl", "--user", "disable", "--", "syncthing.service"}));
}

TEST_CASE("the startup page does not animate", "[gui][startup]") {
    FakeRoot fr;
    Fake fake;
    StartupPage page(std::make_shared<StartupCollector>(emptyDirs(fr), fake.runner()));
    REQUIRE_FALSE(page.animates());
}

TEST_CASE("the users page lists users and does not animate", "[gui][users]") {
    Fake fake;
    UsersPage page(std::make_shared<SessionsCollector>(fake.runner()));
    page.refresh();
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    REQUIRE(page.table()->rowCount() == 1);
    REQUIRE(page.table()->item(0, 0)->text() == "tsoueid");
    REQUIRE_FALSE(page.animates());
}

TEST_CASE("users page folds live process data into the rows", "[gui][users]") {
    Fake fake;
    UsersPage page(std::make_shared<SessionsCollector>(fake.runner()));
    page.refresh();
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    auto f = testing::populatedFrame(8, 3);
    for (auto& r : f.processes.processes) r.user = "tsoueid";
    HistoryStore h;
    page.update(f, h, f.mono);
    REQUIRE(page.table()->item(0, 3)->text() == "6.0%");  // 1% + 2% + 3%
}

TEST_CASE("sign off passes the session id after a double dash", "[gui][users]") {
    Fake fake;
    UsersPage page(std::make_shared<SessionsCollector>(fake.runner()));
    page.setConfirmActions(false);
    page.refresh();
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    page.signOff(0);
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    REQUIRE(fake.called({"loginctl", "terminate-session", "--", "3"}));
}

TEST_CASE("lock uses lock-session", "[gui][users]") {
    Fake fake;
    UsersPage page(std::make_shared<SessionsCollector>(fake.runner()));
    page.setConfirmActions(false);
    page.refresh();
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    page.lockSession(0);
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    REQUIRE(fake.called({"loginctl", "lock-session", "--", "3"}));
}

TEST_CASE("a failed sign off stays visible after the list refreshes", "[gui][users]") {
    Fake fake;
    fake.failSessionActions = true;
    UsersPage page(std::make_shared<SessionsCollector>(fake.runner()));
    page.setConfirmActions(false);
    page.refresh();
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    page.signOff(0);
    REQUIRE(waitFor(&page, &UsersPage::refreshed));  // the refresh that follows the action
    REQUIRE(page.lastError().contains("Interactive authentication required"));
}
