// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/performance_page.hpp>
#include <perfmonplus/gui/processes_page.hpp>
#include <perfmonplus/gui/summary_page.hpp>
#include <perfmonplus/gui/users_page.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>

#include <chrono>
#include <optional>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

ProcessRecord proc(int pid, std::string name, double cpu) {
    ProcessRecord r;
    r.key = {pid, static_cast<std::uint64_t>(pid) * 10};
    r.name = std::move(name);
    r.user = "dave";
    r.state = 'S';
    r.cpu = Metric::measured(cpu, Unit::Percent);
    r.memFootprint = Metric::measured(1000, Unit::Bytes);
    r.memPrivate = Metric::measured(500, Unit::Bytes);
    r.threads = Metric::measured(1, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
    r.gpu = Metric::measured(0, Unit::Percent);
    return r;
}

Frame frameWith(std::vector<ProcessRecord> ps) {
    Frame f = testing::populatedFrame(4, 0);
    testing::withHardware(f);
    f.processes.processes = std::move(ps);
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(10000, Unit::Bytes);
    return f;
}

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

TEST_CASE("double clicking a Summary process opens it in the Processes page", "[gui][links]") {
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.applyFrame({std::make_shared<const Frame>(frameWith({proc(11, "busy", 40), proc(12, "idle", 1)})), false});
    auto* summary = qobject_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    REQUIRE(summary->processTable()->rowCount() == 2);
    summary->processTable()->cellDoubleClicked(0, 1);  // the busiest first
    REQUIRE(w.currentPage() == Page::Processes);
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page->selectedKey() == ProcessKey{11, 110});
}

TEST_CASE("an energy demand row opens its process", "[gui][links]") {
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.applyFrame({std::make_shared<const Frame>(frameWith({proc(21, "hungry", 60), proc(22, "calm", 2)})), false});
    w.showPage(Page::Performance);
    auto* perf = qobject_cast<PerformancePage*>(w.pageWidget(Page::Performance));
    perf->selectSubsystem(vm::Subsystem::Energy);
    w.applyFrame({std::make_shared<const Frame>(frameWith({proc(21, "hungry", 60), proc(22, "calm", 2)})), false});
    QLabel* row = nullptr;
    for (auto* l : perf->findChildren<QLabel*>()) {
        if (l->text() == "hungry") row = l;
    }
    REQUIRE(row);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), row->mapToGlobal(QPointF(5, 5)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(row, &press);
    REQUIRE(w.currentPage() == Page::Processes);
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page->selectedKey() == ProcessKey{21, 210});
}

TEST_CASE("a user row asks for that user's processes", "[gui][links]") {
    linux::CommandRunner runner = [](const std::vector<std::string>& argv) {
        if (argv.size() > 2 && argv[2] == "list-sessions") {
            return linux::CommandResult{0,
                                        R"([{"session":"3","uid":1000,"user":"alice","seat":"seat0","leader":3750,"class":"user","tty":"tty2","idle":false}])",
                                        "", false};
        }
        return linux::CommandResult{0, "", "", false};
    };
    UsersPage page(std::make_shared<linux::SessionsCollector>(runner));
    page.refresh();
    REQUIRE(waitFor(&page, &UsersPage::refreshed));
    std::optional<QString> asked;
    QObject::connect(&page, &UsersPage::userProcessesRequested, &page, [&](const QString& user) { asked = user; });
    page.table()->cellDoubleClicked(0, 0);
    REQUIRE(asked == QString("alice"));
}

TEST_CASE("the main window shows that user's processes", "[gui][links]") {
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.applyFrame({std::make_shared<const Frame>(frameWith({proc(31, "mine", 5)})), false});
    auto* users = qobject_cast<UsersPage*>(w.pageWidget(Page::Users));
    REQUIRE(users);
    emit users->userProcessesRequested("dave");
    REQUIRE(w.currentPage() == Page::Processes);
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page->filterField()->text() == "dave");
    REQUIRE(page->viewCombo()->currentData().toInt() == static_cast<int>(vm::ProcessView::ByUser));
    REQUIRE(page->model()->rowCount() >= 2);  // the user's header and the process
}

TEST_CASE("a process that has exited says so instead of selecting nothing silently", "[gui][links]") {
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.applyFrame({std::make_shared<const Frame>(frameWith({proc(41, "gone-soon", 5)})), false});
    w.goToProcess(ProcessKey{12345, 1});
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page->messageText().contains("has exited"));
}

TEST_CASE("a one-off view is not saved as the user's choice", "[gui][links][minor]") {
    ProcessesPage page;
    page.resize(900, 600);
    int changes = 0;
    QObject::connect(&page, &ProcessesPage::settingsChanged, &page, [&] { ++changes; });
    AppSettings before;
    page.exportSettings(before);
    page.setView(vm::ProcessView::ByUser, false);
    AppSettings during;
    page.exportSettings(during);
    REQUIRE(during.processView == before.processView);  // still the view the user picked
    REQUIRE(changes == 0);
    page.setView(vm::ProcessView::Flat);  // a deliberate change is remembered
    AppSettings after;
    page.exportSettings(after);
    REQUIRE(after.processView == 2);
    REQUIRE(changes == 1);
}

TEST_CASE("a process hidden by the filter is not called exited", "[gui][links][minor]") {
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.applyFrame({std::make_shared<const Frame>(frameWith({proc(41, "alive-but-filtered", 5)})), false});
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    page->setFilter("zzz-matches-nothing");
    w.goToProcess(proc(41, "alive-but-filtered", 5).key);
    REQUIRE(page->messageText() == "That process is hidden by the filter. Clear the filter to see it.");
}
