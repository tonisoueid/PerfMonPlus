// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/gui/processes_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryDir>

#include <chrono>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

MonoTime at(int s) { return MonoTime{} + std::chrono::seconds(s); }

ProcessRecord rec(int pid, std::string name, double cpu = 1, double mem = 1000, ProcessKind kind = ProcessKind::Background) {
    ProcessRecord r;
    r.key = {pid, static_cast<std::uint64_t>(pid) * 10};
    r.name = std::move(name);
    r.user = "dave";
    r.state = 'S';
    r.kind = kind;
    r.cpu = Metric::measured(cpu, Unit::Percent);
    r.memFootprint = Metric::measured(mem, Unit::Bytes);
    r.memPrivate = Metric::measured(mem / 2, Unit::Bytes);
    r.threads = Metric::measured(1, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
    r.gpu = Metric::measured(0, Unit::Percent);
    return r;
}

Frame frameOf(std::vector<ProcessRecord> ps, int t = 100) {
    Frame f;
    f.generation = static_cast<std::uint64_t>(t);
    f.mono = at(t);
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(10000, Unit::Bytes);
    f.processes.processes = std::move(ps);
    return f;
}

struct Fixture {
    ProcessesPage page;
    HistoryStore history;
    Fixture() {
        page.resize(1000, 600);
        page.show();
    }
    void feed(const Frame& f) { page.update(f, history, f.mono); }
    QString cell(int row, int col = 0) { return page.model()->index(row, col).data().toString(); }
    int rows() { return page.model()->rowCount(); }
};

}  // namespace

TEST_CASE("the table lists processes grouped with counts", "[gui][processes]") {
    Fixture fx;
    fx.feed(frameOf({rec(1, "systemd", 0, 100, ProcessKind::System), rec(2, "gedit", 1, 100, ProcessKind::App)}));
    REQUIRE(fx.cell(0) == "Applications (1)");
    REQUIRE(fx.page.table()->columnSpan(0, 0) == fx.page.model()->columnCount());  // a header spans the row
    REQUIRE(fx.cell(1) == "gedit");
    REQUIRE(fx.cell(2) == "System processes (1)");
}

TEST_CASE("the view combo switches modes", "[gui][processes]") {
    Fixture fx;
    auto init = rec(1, "init");
    auto child = rec(2, "child");
    child.parent = init.key;
    fx.feed(frameOf({init, child}));
    fx.page.viewCombo()->setCurrentIndex(1);  // Tree View
    REQUIRE(fx.cell(0) == "init");
    REQUIRE(fx.cell(1) == "child");
    fx.page.viewCombo()->setCurrentIndex(2);  // Flat List
    REQUIRE(fx.rows() == 2);
    fx.page.viewCombo()->setCurrentIndex(3);  // By User
    REQUIRE(fx.cell(0) == "dave (2)");
}

TEST_CASE("filter keeps ancestors and treats text literally", "[gui][processes]") {
    Fixture fx;
    auto init = rec(1, "init");
    auto mid = rec(2, "mid");
    mid.parent = init.key;
    auto needle = rec(3, "needle");
    needle.parent = mid.key;
    fx.feed(frameOf({init, mid, needle, rec(4, "hay")}));
    fx.page.setView(vm::ProcessView::Tree);
    fx.page.setFilter("needle");
    REQUIRE(fx.rows() == 3);
    fx.page.setFilter(".*");
    REQUIRE(fx.rows() == 0);
}

TEST_CASE("header click sorts and reverses, and the indicator follows", "[gui][processes]") {
    Fixture fx;
    fx.page.setView(vm::ProcessView::Flat);
    fx.feed(frameOf({rec(1, "a", 5), rec(2, "b", 9), rec(3, "c", 1)}));
    auto* header = fx.page.table()->horizontalHeader();
    const int cpuSection = 4;  // Name, PID, Status, User name, CPU usage, Memory usage
    header->sectionClicked(cpuSection);
    REQUIRE(fx.cell(0) == "b");  // numbers: biggest first
    REQUIRE(header->sortIndicatorSection() == cpuSection);
    REQUIRE(header->sortIndicatorOrder() == Qt::DescendingOrder);
    header->sectionClicked(cpuSection);
    REQUIRE(fx.cell(0) == "c");
    REQUIRE(header->sortIndicatorOrder() == Qt::AscendingOrder);
    header->sectionClicked(0);  // Name: text sorts A-Z first
    REQUIRE(fx.cell(0) == "a");
}

TEST_CASE("selection follows the process through churn", "[gui][processes]") {
    Fixture fx;
    fx.page.setView(vm::ProcessView::Flat);
    fx.page.sortBy(vm::ProcessColumn::Cpu);  // busiest first
    fx.feed(frameOf({rec(1, "a", 10), rec(2, "b", 5), rec(3, "c", 1)}, 100));
    const ProcessKey b{2, 20};
    REQUIRE(fx.page.selectProcess(b));
    REQUIRE(fx.page.selectedKey() == b);

    // a busier process appears and the order changes: b is now further down but still selected and in view
    fx.feed(frameOf({rec(1, "a", 2), rec(2, "b", 5), rec(3, "c", 1), rec(4, "d", 50)}, 101));
    REQUIRE(fx.page.selectedKey() == b);
    REQUIRE(fx.page.table()->currentIndex().row() == fx.page.model()->rowOfId("p:2:20"));
    REQUIRE(fx.cell(0) == "d");

    // b exits and becomes a tombstone: still there, still selected
    auto gone = rec(2, "b", 0);
    gone.exitedAt = at(102);
    fx.feed(frameOf({rec(1, "a", 2), gone, rec(3, "c", 1), rec(4, "d", 50)}, 103));
    REQUIRE(fx.page.selectedKey() == b);
    REQUIRE(fx.page.model()->data(fx.page.model()->index(fx.page.table()->currentIndex().row(), 0), ProcessTableModel::TombstoneRole).toBool());

    // and when the tombstone expires the selection goes away with it
    fx.feed(frameOf({rec(1, "a", 2), rec(3, "c", 1), rec(4, "d", 50)}, 130));
    REQUIRE_FALSE(fx.page.selectedKey().has_value());
}

TEST_CASE("unchanged data only changes values, never the layout", "[gui][processes]") {
    Fixture fx;
    fx.page.setView(vm::ProcessView::Flat);
    fx.feed(frameOf({rec(1, "a", 5), rec(2, "b", 5)}, 100));
    const int layouts = fx.page.model()->layoutChangeCount();
    fx.feed(frameOf({rec(1, "a", 5.5), rec(2, "b", 5.2)}, 101));
    fx.feed(frameOf({rec(1, "a", 5.7), rec(2, "b", 5.1)}, 102));
    REQUIRE(fx.page.model()->layoutChangeCount() == layouts);
    REQUIRE(fx.page.model()->dataChangeCount() >= 2);
}

TEST_CASE("rich text in a process name is shown literally", "[gui][processes]") {
    Fixture fx;
    fx.page.setView(vm::ProcessView::Flat);
    fx.feed(frameOf({rec(1, "<b>x</b> <a href=\"http://evil\">y</a>")}));
    REQUIRE(fx.cell(0) == "<b>x</b> <a href=\"http://evil\">y</a>");
    REQUIRE_FALSE(fx.page.model()->index(0, 0).data(Qt::ToolTipRole).isValid());  // never offered as a tooltip
    for (auto* label : fx.page.findChildren<QLabel*>()) REQUIRE(label->textFormat() == Qt::PlainText);  // nothing is parsed as markup
}

TEST_CASE("the column menu toggles optional columns and reports a change", "[gui][processes]") {
    Fixture fx;
    int changed = 0;
    std::optional<bool> collecting;
    QObject::connect(&fx.page, &ProcessesPage::settingsChanged, &fx.page, [&] { ++changed; });
    QObject::connect(&fx.page, &ProcessesPage::commandLineCollectionRequested, &fx.page, [&](bool on) { collecting = on; });
    REQUIRE(fx.page.model()->columnCount() == 6);
    fx.page.setColumnVisible(vm::ProcessColumn::Threads, true);
    REQUIRE(fx.page.model()->columnCount() == 7);
    REQUIRE(changed == 1);
    fx.page.setColumnVisible(vm::ProcessColumn::Name, false);  // Name cannot be hidden
    REQUIRE(fx.page.model()->columnCount() == 7);
    REQUIRE(fx.page.columns().front() == vm::ProcessColumn::Name);
    fx.page.setColumnVisible(vm::ProcessColumn::CommandLine, true);
    REQUIRE(collecting == true);
    fx.page.setColumnVisible(vm::ProcessColumn::CommandLine, false);
    REQUIRE(collecting == false);
}

TEST_CASE("tombstone rows are marked and respect the retention setting", "[gui][processes]") {
    Fixture fx;
    fx.page.setView(vm::ProcessView::Flat);
    auto gone = rec(2, "gone");
    gone.exitedAt = at(97);
    fx.feed(frameOf({rec(1, "live"), gone}, 100));
    REQUIRE(fx.rows() == 2);
    REQUIRE(fx.page.model()->data(fx.page.model()->index(1, 0), ProcessTableModel::TombstoneRole).toBool());
    fx.page.setTombstoneSeconds(0);
    REQUIRE(fx.rows() == 1);
}

TEST_CASE("expanding an app shows its members; collapsing hides them", "[gui][processes]") {
    Fixture fx;
    auto a = rec(5, "ff", 1, 100, ProcessKind::App);
    a.appScope = "s";
    auto b = rec(6, "ff-web", 1, 100, ProcessKind::App);
    b.appScope = "s";
    b.parent = a.key;
    fx.feed(frameOf({a, b}));
    REQUIRE(fx.rows() == 2);  // header and the collapsed group
    fx.page.toggleExpanded(1);
    REQUIRE(fx.rows() == 4);
    fx.page.toggleExpanded(1);
    REQUIRE(fx.rows() == 2);
}

TEST_CASE("the arrow keys expand and collapse", "[gui][processes]") {
    Fixture fx;
    auto a = rec(5, "ff", 1, 100, ProcessKind::App);
    a.appScope = "s";
    auto b = rec(6, "ff-web", 1, 100, ProcessKind::App);
    b.appScope = "s";
    b.parent = a.key;
    fx.feed(frameOf({a, b}));
    fx.page.table()->setCurrentIndex(fx.page.model()->index(1, 0));
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(fx.page.table(), &right);
    REQUIRE(fx.rows() == 4);
    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(fx.page.table(), &left);
    REQUIRE(fx.rows() == 2);
}

TEST_CASE("selecting a hidden process opens what hides it, and it stays open", "[gui][processes]") {
    Fixture fx;
    auto a = rec(5, "ff", 1, 100, ProcessKind::App);
    a.appScope = "s";
    auto b = rec(6, "ff-web", 1, 100, ProcessKind::App);
    b.appScope = "s";
    b.parent = a.key;
    fx.feed(frameOf({a, b}, 100));
    REQUIRE(fx.page.selectProcess(b.key));
    REQUIRE(fx.page.selectedKey() == b.key);
    fx.feed(frameOf({a, b}, 101));  // the next sample must not fold the group again
    REQUIRE(fx.page.selectedKey() == b.key);
    REQUIRE_FALSE(fx.page.selectProcess(ProcessKey{999, 1}));  // not in the table
}

TEST_CASE("the page does nothing while hidden", "[gui][processes]") {
    ProcessesPage page;
    HistoryStore h;
    const auto f = frameOf({rec(1, "a")});
    page.update(f, h, f.mono);
    REQUIRE(page.rebuildCount() == 0);
    page.show();
    page.update(f, h, f.mono);
    REQUIRE(page.rebuildCount() == 1);
    REQUIRE_FALSE(page.animates());
}

TEST_CASE("process settings survive a save and load, and bad values fall back", "[gui][processes][settings]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    AppSettings s;
    s.processView = 2;
    s.processSort = "cpu";
    s.processSortDescending = false;
    s.processColumns = {"name", "pid", "threads", "path"};
    s.tombstoneSeconds = 30;
    s.save(store);
    store.sync();
    REQUIRE(AppSettings::load(store) == s);

    store.setValue("processView", 9);
    store.setValue("processSort", "nonsense");
    store.setValue("processColumns", QStringList{"cpu", "bogus", "memory"});
    store.setValue("tombstoneSeconds", 7);
    const auto r = AppSettings::load(store);
    REQUIRE(r.processView == 3);
    REQUIRE(r.processSort == "memory");
    REQUIRE(r.processColumns == QStringList{"name", "cpu", "memory"});  // Name is always first
    REQUIRE(r.tombstoneSeconds == 10);
}

TEST_CASE("the page applies and exports its settings", "[gui][processes][settings]") {
    Fixture fx;
    AppSettings s;
    s.processView = 2;
    s.processSort = "cpu";
    s.processSortDescending = false;
    s.processColumns = {"name", "pid", "cpu"};
    s.tombstoneSeconds = 5;
    fx.page.applySettings(s);
    REQUIRE(fx.page.model()->columnCount() == 3);
    REQUIRE(fx.page.viewCombo()->currentIndex() == 2);
    AppSettings out;
    fx.page.exportSettings(out);
    REQUIRE(out.processView == 2);
    REQUIRE(out.processSort == "cpu");
    REQUIRE_FALSE(out.processSortDescending);
    REQUIRE(out.processColumns == QStringList{"name", "pid", "cpu"});
    REQUIRE(out.tombstoneSeconds == 5);
}

TEST_CASE("selection survives an app growing from one process to a group and back", "[gui][processes]") {
    Fixture fx;  // the default Process View
    auto a = rec(5, "ff", 1, 100, ProcessKind::App);
    a.appScope = "s";
    auto b = rec(6, "ff-web", 1, 100, ProcessKind::App);
    b.appScope = "s";
    b.parent = a.key;
    fx.feed(frameOf({a}, 100));
    REQUIRE(fx.page.selectProcess(a.key));
    REQUIRE(fx.page.selectedKey() == a.key);
    fx.feed(frameOf({a, b}, 101));  // a second process joins: the row becomes a collapsed group
    REQUIRE(fx.page.selectedKey() == a.key);
    REQUIRE(fx.page.model()->rowOfId("a:s") == fx.page.table()->currentIndex().row());
    fx.feed(frameOf({b}, 102));  // the first one exits: a lone process again
    REQUIRE(fx.page.selectedKey() == b.key);
}

TEST_CASE("the selected row is kept in view when the sort moves it", "[gui][processes]") {
    ProcessesPage page;
    page.resize(700, 320);
    page.show();
    page.setView(vm::ProcessView::Flat);
    page.sortBy(vm::ProcessColumn::Cpu);
    HistoryStore h;
    const auto feed = [&](int top) {
        std::vector<ProcessRecord> ps;
        for (int i = 1; i <= 60; ++i) ps.push_back(rec(i, "p" + std::to_string(i), i == top ? 90 : 60 - i));
        const auto f = frameOf(ps, 100 + top);
        page.update(f, h, f.mono);
    };
    feed(1);  // p1 first
    REQUIRE(page.selectProcess({1, 10}));
    const auto visible = [&] {
        const auto idx = page.table()->currentIndex();
        return idx.isValid() && page.table()->viewport()->rect().intersects(page.table()->visualRect(idx));
    };
    REQUIRE(visible());
    feed(0);  // nobody is at 90 now: p1 drops to 59 -> still near the top; push it to the very bottom instead
    std::vector<ProcessRecord> ps;
    for (int i = 1; i <= 60; ++i) ps.push_back(rec(i, "p" + std::to_string(i), i == 1 ? 0.0 : 100.0 - i));
    const auto f = frameOf(ps, 300);
    page.update(f, h, f.mono);
    REQUIRE(page.model()->rowOfId("p:1:10") == 59);  // last
    REQUIRE(visible());
}

TEST_CASE("showing or hiding a column keeps the selected process", "[gui][processes][minor]") {
    ProcessesPage page;
    page.resize(1000, 600);
    page.show();
    HistoryStore h;
    Frame f;
    f.generation = 1;
    f.mono = MonoTime{} + std::chrono::seconds(1);
    ProcessRecord r;
    r.key = {42, 7};
    r.name = "keep-me";
    r.cpu = Metric::measured(5, Unit::Percent);
    r.memFootprint = Metric::measured(1e6, Unit::Bytes);
    f.processes.processes.push_back(r);
    f.processes.count = Metric::measured(1, Unit::Count);
    page.update(f, h, f.mono);
    REQUIRE(page.selectProcess(r.key));
    page.setColumnVisible(vm::ProcessColumn::Pid, false);
    REQUIRE(page.selectedKey().has_value());
    REQUIRE(page.selectedKey()->pid == 42);
    page.setColumnVisible(vm::ProcessColumn::Pid, true);
    REQUIRE(page.selectedKey()->pid == 42);
}

TEST_CASE("a group row says that actions reach only its main process", "[gui][processes][minor]") {
    ProcessesPage page;
    page.resize(1000, 600);
    page.show();
    HistoryStore h;
    Frame f;
    f.generation = 1;
    f.mono = MonoTime{} + std::chrono::seconds(1);
    for (int i = 0; i < 3; ++i) {
        ProcessRecord r;
        r.key = {100 + i, 7u + static_cast<unsigned>(i)};
        r.name = i == 0 ? "firefox" : "firefox-child";
        r.kind = ProcessKind::App;
        r.appScope = "app-firefox";
        if (i > 0) r.parent = ProcessKey{100, 7};
        r.cpu = Metric::measured(5, Unit::Percent);
        r.memFootprint = Metric::measured(1e6, Unit::Bytes);
        f.processes.processes.push_back(r);
    }
    f.processes.count = Metric::measured(3, Unit::Count);
    page.update(f, h, f.mono);
    int groupRow = -1, plainRow = -1;
    for (int r = 0; r < page.model()->rowCount(); ++r) {
        if (page.model()->rowAt(r).type == vm::TableRow::Type::Group) groupRow = r;
        if (page.model()->rowAt(r).type == vm::TableRow::Type::Process && plainRow < 0) plainRow = r;
    }
    REQUIRE(groupRow >= 0);
    page.table()->setCurrentIndex(page.model()->index(groupRow, 0));
    page.table()->selectRow(groupRow);
    QString tip;
    for (auto* b : page.findChildren<QToolButton*>()) {
        if (b->text() == "End task") tip = b->toolTip();
    }
    REQUIRE(tip.contains("main process only; 2 other processes keep running"));
    if (plainRow >= 0) {
        page.table()->setCurrentIndex(page.model()->index(plainRow, 0));
        page.table()->selectRow(plainRow);
        for (auto* b : page.findChildren<QToolButton*>()) {
            if (b->text() == "End task") REQUIRE(b->toolTip().isEmpty());
        }
    }
}
