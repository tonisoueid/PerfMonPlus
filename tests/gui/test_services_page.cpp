// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/services_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <mutex>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::linux;

namespace {

constexpr const char* kUnits =
    R"([{"unit":"accounts-daemon.service","load":"loaded","active":"active","sub":"running","description":"Accounts Service"},)"
    R"({"unit":"alsa-restore.service","load":"loaded","active":"active","sub":"exited","description":"Save/Restore Sound Card State"}])";
constexpr const char* kFiles = R"([{"unit_file":"accounts-daemon.service","state":"enabled"},{"unit_file":"alsa-restore.service","state":"static"}])";

struct Fake {
    std::mutex m;
    std::vector<std::vector<std::string>> calls;
    bool failActions = false;
    std::string units = kUnits;  // swapped by a test to reorder the list between refreshes
    CommandRunner runner() {
        return [this](const std::vector<std::string>& argv) {
            std::lock_guard lock(m);
            calls.push_back(argv);
            const auto has = [&](const char* w) { return std::find(argv.begin(), argv.end(), w) != argv.end(); };
            if (has("list-units")) return CommandResult{0, units, "", false};
            if (has("list-unit-files")) return CommandResult{0, kFiles, "", false};
            if (has("show")) return CommandResult{0, "Id=accounts-daemon.service\nMainPID=812\n", "", false};
            if (failActions) return CommandResult{1, "", "Interactive authentication required.", false};
            return CommandResult{0, "", "", false};
        };
    }
    std::size_t count(const char* word) {
        std::lock_guard lock(m);
        std::size_t n = 0;
        for (const auto& c : calls) n += std::find(c.begin(), c.end(), word) != c.end();
        return n;
    }
    std::vector<std::string> last() {
        std::lock_guard lock(m);
        return calls.back();
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

}  // namespace

TEST_CASE("the services page fills its table after a refresh", "[gui][services]") {
    Fake fake;
    ServicesPage page(std::make_shared<ServicesCollector>(fake.runner()));
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    REQUIRE(page.table()->rowCount() == 2);
    REQUIRE(page.table()->item(0, 0)->text() == "accounts-daemon.service");
    REQUIRE(page.table()->item(0, 2)->text() == "Running");
    REQUIRE(page.lastError().isEmpty());
}

TEST_CASE("the filter box narrows the table", "[gui][services]") {
    Fake fake;
    ServicesPage page(std::make_shared<ServicesCollector>(fake.runner()));
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    page.filterField()->setText("alsa");
    REQUIRE(page.table()->rowCount() == 1);
}

TEST_CASE("actions use the selected unit after a double dash", "[gui][services]") {
    Fake fake;
    ServicesPage page(std::make_shared<ServicesCollector>(fake.runner()));
    page.setConfirmActions(false);
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    page.runAction("restart", 0);
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));  // the list is refreshed after the action
    bool found = false;
    {
        std::lock_guard lock(fake.m);
        for (const auto& c : fake.calls) {
            if (c.size() == 4 && c[1] == "restart") {
                found = true;
                REQUIRE(c[2] == "--");
                REQUIRE(c[3] == "accounts-daemon.service");
            }
        }
    }
    REQUIRE(found);
}

TEST_CASE("failed action reports and refreshes", "[gui][services]") {
    Fake fake;
    fake.failActions = true;
    ServicesPage page(std::make_shared<ServicesCollector>(fake.runner()));
    page.setConfirmActions(false);
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    const auto before = fake.count("list-units");
    page.runAction("restart", 0);
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    REQUIRE(page.lastError().contains("Interactive authentication required"));
    REQUIRE(fake.count("list-units") > before);
}

TEST_CASE("a failing list shows the error and keeps the page alive", "[gui][services]") {
    ServicesPage page(std::make_shared<ServicesCollector>([](const std::vector<std::string>&) {
        return CommandResult{1, "", "Failed to connect to bus", false};
    }));
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    REQUIRE(page.lastError().contains("Failed to connect to bus"));
    REQUIRE(page.table()->rowCount() == 0);
}

TEST_CASE("the services page does not animate", "[gui][services]") {
    Fake fake;
    ServicesPage page(std::make_shared<ServicesCollector>(fake.runner()));
    REQUIRE_FALSE(page.animates());
}

TEST_CASE("the selection follows its unit when the list reorders", "[gui][services]") {
    Fake fake;
    ServicesPage page(std::make_shared<ServicesCollector>(fake.runner()));
    page.setConfirmActions(false);
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    page.table()->setCurrentCell(1, 0);
    REQUIRE(page.table()->item(1, 0)->text() == "alsa-restore.service");
    {
        std::lock_guard lock(fake.m);  // a transient unit appears above the selected one
        fake.units =
            R"([{"unit":"aaa-new.service","load":"loaded","active":"active","sub":"running","description":"New"},)"
            R"({"unit":"accounts-daemon.service","load":"loaded","active":"active","sub":"running","description":"Accounts Service"},)"
            R"({"unit":"alsa-restore.service","load":"loaded","active":"active","sub":"exited","description":"Sound"}])";
    }
    page.refresh();
    REQUIRE(waitFor(&page, &ServicesPage::refreshed));
    REQUIRE(page.table()->rowCount() == 3);
    REQUIRE(page.table()->currentRow() >= 0);
    REQUIRE(page.table()->item(page.table()->currentRow(), 0)->text() == "alsa-restore.service");
}
