// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/installed_apps_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <mutex>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::linux;

namespace {

InstalledApp app(const std::string& id, AppSource src, double bytes, bool user = false) {
    InstalledApp a;
    a.id = a.name = id;
    a.source = src;
    a.userInstall = user;
    a.version = "1";
    if (bytes >= 0) a.sizeBytes = Metric::measured(bytes, Unit::Bytes);
    return a;
}

template <class Cond>
bool waitFor(Cond cond, int ms = 2000) {
    QEventLoop loop;
    QTimer poll, limit;
    limit.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cond()) loop.quit();
    });
    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10);
    limit.start(ms);
    loop.exec();
    return cond();
}

struct Recorder {
    std::mutex m;
    std::vector<std::vector<std::string>> commands;
    int confirms = 0;
    bool answer = true;
    QStringList shown, revealed;
};

InstalledAppsPage::CollectFn fixed(std::vector<InstalledApp> apps) {
    return [apps](const std::atomic<bool>&, std::vector<std::string>*) { return apps; };
}

CommandRunner recording(std::shared_ptr<Recorder> r, int exitCode = 0) {
    return [r, exitCode](const std::vector<std::string>& argv) {
        std::lock_guard<std::mutex> lock(r->m);
        r->commands.push_back(argv);
        CommandResult c;
        c.exitCode = exitCode;
        c.err = exitCode ? "boom" : "";
        return c;
    };
}

InstalledAppsPage::Hooks hooksFor(std::shared_ptr<Recorder> r) {
    InstalledAppsPage::Hooks h;
    h.confirm = [r](const QString&, const QString&) {
        ++r->confirms;
        return r->answer;
    };
    h.showCommand = [r](const QString& c) { r->shown << c; };
    h.reveal = [r](const QString& p) { r->revealed << p; };
    return h;
}

}  // namespace

TEST_CASE("the page reads nothing until it is shown", "[gui][apps]") {
    std::atomic<int> calls{0};
    InstalledAppsPage page(
        [&](const std::atomic<bool>&, std::vector<std::string>*) {
            ++calls;
            return std::vector<InstalledApp>{app("a", AppSource::Dpkg, 10)};
        },
        runDefault, [] { return std::vector<KernelModule>{}; });
    REQUIRE(calls.load() == 0);
    REQUIRE_FALSE(page.everLoaded());
    page.show();
    REQUIRE(waitFor([&] { return page.everLoaded(); }));
    REQUIRE(calls.load() == 1);
    REQUIRE(page.model()->rowCount() == 1);
    page.hide();
    page.show();
    waitFor([] { return false; }, 100);
    REQUIRE(calls.load() == 1);  // showing it again does not re-read
}

TEST_CASE("sizes read Calculating… until the list is complete, and Cancel stops it", "[gui][apps]") {
    std::atomic<bool> release{false};
    std::atomic<bool> sawCancel{false};
    InstalledAppsPage page(
        [&](const std::atomic<bool>& cancel, std::vector<std::string>*) {
            while (!release && !cancel) QThread::msleep(5);
            sawCancel = cancel.load();
            return std::vector<InstalledApp>{app("partial", AppSource::Dpkg, 10)};
        },
        runDefault, [] { return std::vector<KernelModule>{}; });
    release = true;
    page.refresh();
    REQUIRE(waitFor([&] { return !page.loading(); }));
    release = false;
    page.refresh();
    REQUIRE(page.loading());
    REQUIRE(page.cancelButton()->isHidden() == false);
    REQUIRE(page.model()->data(page.model()->index(0, 4), Qt::DisplayRole).toString() == "Calculating…");
    page.cancelButton()->click();
    REQUIRE(waitFor([&] { return !page.loading(); }));
    REQUIRE(sawCancel.load());
    REQUIRE(page.statusText().contains("partial"));
    REQUIRE(page.cancelButton()->isHidden());
    REQUIRE(page.model()->data(page.model()->index(0, 4), Qt::DisplayRole).toString() != "Calculating…");
}

TEST_CASE("the summary bar and the unknown-size note", "[gui][apps]") {
    InstalledAppsPage page(fixed({app("a", AppSource::Dpkg, 2e9), app("b", AppSource::Snap, 1e9), app("c", AppSource::Dpkg, -1)}), runDefault,
                           [] { return std::vector<KernelModule>{}; });
    page.refreshSync();
    REQUIRE(page.totalText().contains("in 3 apps"));
    REQUIRE(page.noteText() == "1 app without a known size is not counted");
    REQUIRE(page.sizeBar()->segmentCount() == 2);
    REQUIRE(page.model()->rowAt(0).name == "a");  // sorted by size, descending
}

TEST_CASE("uninstall of a dpkg app executes nothing and shows the command", "[gui][apps]") {
    auto rec = std::make_shared<Recorder>();
    InstalledAppsPage page(fixed({app("bash", AppSource::Dpkg, 10)}), recording(rec), [] { return std::vector<KernelModule>{}; });
    page.setHooks(hooksFor(rec));
    page.refreshSync();
    page.table()->selectRow(0);
    page.uninstallButton()->click();
    waitFor([] { return false; }, 100);
    REQUIRE(rec->commands.empty());  // nothing was run
    REQUIRE(rec->shown == QStringList{"sudo apt remove -- bash"});
    REQUIRE(rec->confirms == 0);
}

TEST_CASE("uninstall of a user flatpak asks, then runs exactly the per-user command", "[gui][apps]") {
    auto rec = std::make_shared<Recorder>();
    InstalledAppsPage page(fixed({app("org.x.App", AppSource::Flatpak, 10, true)}), recording(rec), [] { return std::vector<KernelModule>{}; });
    page.setHooks(hooksFor(rec));
    page.refreshSync();
    page.table()->selectRow(0);
    rec->answer = false;  // Cancel
    page.uninstallSelected();
    waitFor([] { return false; }, 100);
    REQUIRE(rec->confirms == 1);
    REQUIRE(rec->commands.empty());
    rec->answer = true;
    page.uninstallSelected();
    REQUIRE(waitFor([&] { return !rec->commands.empty(); }));
    REQUIRE(rec->commands[0] == std::vector<std::string>{"flatpak", "uninstall", "--user", "-y", "--", "org.x.App"});
    REQUIRE(waitFor([&] { return page.statusText().contains("was removed"); }));
}

TEST_CASE("a failed flatpak removal reports the failure", "[gui][apps]") {
    auto rec = std::make_shared<Recorder>();
    InstalledAppsPage page(fixed({app("org.x.App", AppSource::Flatpak, 10, true)}), recording(rec, 1), [] { return std::vector<KernelModule>{}; });
    page.setHooks(hooksFor(rec));
    page.refreshSync();
    page.table()->selectRow(0);
    page.uninstallSelected();
    REQUIRE(waitFor([&] { return page.statusText().contains("Could not remove") && page.statusText().contains("boom"); }));
}

TEST_CASE("system flatpaks, snaps and appimages never run a removal", "[gui][apps]") {
    auto rec = std::make_shared<Recorder>();
    InstalledAppsPage page(fixed({app("sys.App", AppSource::Flatpak, 3, false), app("snapper", AppSource::Snap, 2), app("tool.AppImage", AppSource::AppImage, 1, true)}),
                           recording(rec), [] { return std::vector<KernelModule>{}; });
    page.setHooks(hooksFor(rec));
    page.refreshSync();
    for (int i = 0; i < 3; ++i) {
        page.table()->selectRow(i);
        page.uninstallSelected();
    }
    waitFor([] { return false; }, 100);
    REQUIRE(rec->commands.empty());
    REQUIRE(rec->shown.size() == 2);
    REQUIRE(rec->shown[0] == "sudo flatpak uninstall -- sys.App");
    REQUIRE(rec->shown[1] == "sudo snap remove -- snapper");
    REQUIRE(page.statusText().contains("single file"));
}

TEST_CASE("reveal opens the folder, never the file", "[gui][apps]") {
    auto rec = std::make_shared<Recorder>();
    auto a = app("tool.AppImage", AppSource::AppImage, 1, true);
    a.location = "/proc/self/exe";  // an existing file
    auto b = app("nolocation", AppSource::Dpkg, 1);
    InstalledAppsPage page(fixed({a, b}), recording(rec), [] { return std::vector<KernelModule>{}; });
    page.setHooks(hooksFor(rec));
    page.refreshSync();
    const int none = page.model()->rowAt(0).name == "nolocation" ? 0 : 1;  // equal sizes: ordered by name
    page.table()->selectRow(none);
    REQUIRE_FALSE(page.revealButton()->isEnabled());  // no location known
    page.table()->selectRow(1 - none);
    REQUIRE(page.revealButton()->isEnabled());
    page.revealSelected();
    REQUIRE(rec->revealed.size() == 1);
    REQUIRE(rec->revealed[0].startsWith("/proc"));
    REQUIRE_FALSE(rec->revealed[0].endsWith("exe"));
}

TEST_CASE("hostile names render as plain text and copy is one line", "[gui][apps]") {
    InstalledAppsPage page(fixed({app("<b>x</b>\nname", AppSource::Dpkg, 10)}), runDefault, [] { return std::vector<KernelModule>{}; });
    page.refreshSync();
    page.table()->selectRow(0);
    REQUIRE(page.model()->data(page.model()->index(0, 0), Qt::DisplayRole).toString().contains("<b>x</b>"));
    page.copyButton()->click();
    REQUIRE(QApplication::clipboard()->text().contains('\t'));
}

TEST_CASE("the Kernel modules tab lists modules sorted by size with their users", "[gui][apps][modules]") {
    std::vector<KernelModule> mods(2);
    mods[0] = {"small", 100, 0, {}, "Live"};
    mods[1] = {"big", 90000, 1, {"small"}, "Live"};
    InstalledAppsPage page(fixed({}), runDefault, [mods] { return mods; });
    REQUIRE(page.modulesTable()->rowCount() == 0);
    page.tabs()->setCurrentIndex(1);
    REQUIRE(waitFor([&] { return page.modulesTable()->rowCount() == 2; }));
    REQUIRE(page.modulesTable()->item(0, 0)->text() == "big");
    REQUIRE(page.modulesTable()->item(0, 2)->text() == "small");
    REQUIRE(page.modulesTable()->item(1, 2)->text() == "—");
}

TEST_CASE("filtering and selection survive a refresh", "[gui][apps]") {
    InstalledAppsPage page(fixed({app("alpha", AppSource::Dpkg, 5), app("beta", AppSource::Snap, 9)}), runDefault, [] { return std::vector<KernelModule>{}; });
    page.refreshSync();
    page.table()->selectRow(1);
    const auto key = page.model()->rowAt(1).key;
    page.refreshSync();
    REQUIRE(page.table()->selectionModel()->selectedRows().first().row() == 1);
    REQUIRE(page.model()->rowAt(1).key == key);
    page.filterEdit()->setText("alp");
    REQUIRE(page.model()->rowCount() == 1);
    page.filterEdit()->setText("");
    page.sourceBox()->setCurrentIndex(page.sourceBox()->findText("Snap"));
    REQUIRE(page.model()->rowCount() == 1);
    REQUIRE(page.model()->rowAt(0).name == "beta");
}

TEST_CASE("Reveal works for an apt package by locating its folder when asked", "[gui][apps][minor]") {
    auto rec = std::make_shared<Recorder>();
    InstalledAppsPage page(fixed({app("firefox", AppSource::Dpkg, 5), app("coreutils", AppSource::Dpkg, 4)}), recording(rec),
                           [] { return std::vector<KernelModule>{}; });
    page.setHooks(hooksFor(rec));
    page.setLocator([](const InstalledApp& a) { return a.id == "firefox" ? std::string("/usr/lib/firefox") : std::string(); });
    page.refreshSync();
    page.table()->selectRow(0);  // firefox (the larger)
    REQUIRE(page.revealButton()->isEnabled());
    page.revealSelected();
    REQUIRE(rec->revealed == QStringList{"/usr/lib/firefox"});
    page.table()->selectRow(1);
    page.revealSelected();  // coreutils: nothing to open, and it says why
    REQUIRE(rec->revealed.size() == 1);
    REQUIRE(page.statusText().contains("no folder of its own"));
}
