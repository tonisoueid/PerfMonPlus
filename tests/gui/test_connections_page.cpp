// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/connections_page.hpp>
#include <perfmonplus/gui/main_window.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QTimer>

#include <atomic>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::linux;

namespace {

IpAddr v4(int a, int b, int c, int d) {
    IpAddr x;
    x.bytes = {static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(d)};
    return x;
}

Connection listenerOn(IpAddr a, std::uint16_t port) {
    Connection c;
    c.local = a;
    c.localPort = port;
    c.state = SockState::Listen;
    c.inode = 100 + port;
    c.owner = ProcessKey{10, 1};
    c.ownerAvailability = Availability::Measured;
    return c;
}

Connection conn(std::uint16_t lport, IpAddr remote, std::uint16_t rport) {
    Connection c;
    c.local = v4(10, 0, 0, 2);
    c.localPort = lport;
    c.remote = remote;
    c.remotePort = rport;
    c.state = SockState::Established;
    c.inode = 900 + lport;
    c.owner = ProcessKey{10, 1};
    c.ownerAvailability = Availability::Measured;
    return c;
}

Frame frameWith(const std::string& name) {
    Frame f;
    ProcessRecord r;
    r.key = {10, 1};
    r.name = name;
    f.processes.processes.push_back(r);
    return f;
}

ConnectionList listOf(std::vector<Connection> rows, int truncated = 0) {
    ConnectionList l;
    l.rows = std::move(rows);
    l.truncated = truncated;
    return l;
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

}  // namespace

TEST_CASE("polling runs only while the page is visible", "[gui][connections]") {
    std::atomic<int> calls{0};
    ConnectionsPage page([&] {
        ++calls;
        return listOf({listenerOn(v4(0, 0, 0, 0), 22)});
    });
    REQUIRE_FALSE(page.timerActive());
    page.show();
    REQUIRE(page.timerActive());
    REQUIRE(waitFor([&] { return calls.load() >= 1 && page.model()->rowCount() == 1; }));
    page.hide();
    REQUIRE_FALSE(page.timerActive());
    page.setPaused(true);
    page.show();
    REQUIRE_FALSE(page.timerActive());  // a running benchmark keeps it quiet
    page.setPaused(false);
    REQUIRE(page.timerActive());
}

TEST_CASE("rows show the process name, and rx/tx say unavailable", "[gui][connections]") {
    ConnectionsPage page([] { return ConnectionList{}; });
    page.update(frameWith("sshd"), HistoryStore(10), MonoTime{});
    // feed through the public path: refreshSync uses the collector
    ConnectionsPage p2([] { return listOf({listenerOn(v4(0, 0, 0, 0), 22), conn(5000, v4(1, 2, 3, 4), 443)}); });
    p2.update(frameWith("sshd"), HistoryStore(10), MonoTime{});
    p2.refreshSync();
    REQUIRE(p2.model()->rowCount() == 2);
    const auto idx = p2.model()->index(0, 0);
    REQUIRE(p2.model()->data(idx, Qt::DisplayRole).toString() == "sshd (PID 10)");
    REQUIRE(p2.model()->data(p2.model()->index(0, 5), Qt::DisplayRole).toString() == "Unavailable");
    REQUIRE(p2.model()->data(p2.model()->index(0, 6), Qt::DisplayRole).toString() == "Unavailable");
    REQUIRE(p2.statusText().startsWith("2 connections · 1 exposed"));
}

TEST_CASE("hostile process names are plain text", "[gui][connections]") {
    ConnectionsPage page([] { return listOf({listenerOn(v4(0, 0, 0, 0), 22)}); });
    page.update(frameWith("<b>x</b>\nname"), HistoryStore(10), MonoTime{});
    page.refreshSync();
    const auto text = page.model()->data(page.model()->index(0, 0), Qt::DisplayRole).toString();
    REQUIRE(text.contains("<b>x</b>"));
    REQUIRE(page.model()->data(page.model()->index(0, 0), Qt::DisplayRole).userType() == QMetaType::QString);
    REQUIRE(page.noteText().isEmpty());
}

TEST_CASE("listening mode explains exposure and filters", "[gui][connections]") {
    ConnectionsPage page([] { return listOf({listenerOn(v4(127, 0, 0, 1), 631), listenerOn(v4(0, 0, 0, 0), 22), conn(5000, v4(1, 2, 3, 4), 443)}); });
    page.update(frameWith("cupsd"), HistoryStore(10), MonoTime{});
    page.refreshSync();
    REQUIRE(page.model()->rowCount() == 3);
    page.listeningBox()->setChecked(true);
    REQUIRE(page.model()->rowCount() == 2);
    REQUIRE(page.noteText().contains("firewall"));
    REQUIRE(page.model()->rowAt(0).local == "0.0.0.0:22");  // exposed first
    page.filterEdit()->setText("631");
    REQUIRE(page.model()->rowCount() == 1);
    page.filterEdit()->setText("");
    page.listeningBox()->setChecked(false);
    page.protoBox()->setCurrentIndex(page.protoBox()->findText("UDP"));
    REQUIRE(page.model()->rowCount() == 0);
}

TEST_CASE("selection follows the connection through refreshes", "[gui][connections]") {
    int generation = 0;
    ConnectionsPage page([&] {
        ++generation;
        std::vector<Connection> rows = {conn(5001, v4(1, 1, 1, 1), 1), conn(5002, v4(2, 2, 2, 2), 2)};
        if (generation > 1) rows.insert(rows.begin(), conn(5000, v4(0, 0, 1, 1), 9));  // a new one sorts in front
        return listOf(rows);
    });
    page.update(frameWith("a"), HistoryStore(10), MonoTime{});
    page.refreshSync();
    page.selectKey(QString::fromStdString(page.model()->rowAt(1).key));
    const auto key = page.selectedKey();
    page.refreshSync();
    REQUIRE(page.model()->rowCount() == 3);
    REQUIRE(page.selectedKey() == key);
}

TEST_CASE("go to process needs a live owner", "[gui][connections]") {
    ConnectionsPage page([] { return listOf({conn(5000, v4(1, 1, 1, 1), 1)}); });
    std::vector<ProcessKey> asked;
    QObject::connect(&page, &ConnectionsPage::processRequested, &page, [&](ProcessKey k) { asked.push_back(k); });
    page.refreshSync();
    page.table()->selectRow(0);
    REQUIRE_FALSE(page.canGoToProcess());  // the process table does not know pid 10 yet
    page.update(frameWith("curl"), HistoryStore(10), MonoTime{});
    page.table()->selectRow(0);
    REQUIRE(page.canGoToProcess());
    REQUIRE(page.goButton()->isEnabled());
    page.goToProcess();
    REQUIRE(asked.size() == 1);
    REQUIRE(asked[0].pid == 10);
    // the process exits: the action is no longer offered
    Frame gone = frameWith("curl");
    gone.processes.processes[0].exitedAt = MonoTime{};
    page.update(gone, HistoryStore(10), MonoTime{});
    page.table()->selectRow(0);
    REQUIRE_FALSE(page.canGoToProcess());
}

TEST_CASE("rows from other users show Restricted and cannot be navigated", "[gui][connections]") {
    auto c = conn(5000, v4(1, 1, 1, 1), 1);
    c.owner.reset();
    c.ownerAvailability = Availability::Restricted;
    ConnectionList l = listOf({c});
    l.anyUnreadableOwners = true;
    ConnectionsPage page([l] { return l; });
    page.refreshSync();
    REQUIRE(page.model()->data(page.model()->index(0, 0), Qt::DisplayRole).toString() == "Restricted");
    REQUIRE(page.statusText().contains("other users"));
    page.table()->selectRow(0);
    REQUIRE_FALSE(page.canGoToProcess());
}

TEST_CASE("copy puts one tab-separated plain line on the clipboard", "[gui][connections]") {
    ConnectionsPage page([] { return listOf({listenerOn(v4(0, 0, 0, 0), 22)}); });
    page.update(frameWith("sshd"), HistoryStore(10), MonoTime{});
    page.refreshSync();
    page.table()->selectRow(0);
    page.copyButton()->click();
    REQUIRE(QApplication::clipboard()->text() == "sshd (PID 10)\tTCP\t0.0.0.0:22\t—\tLISTEN");
}

TEST_CASE("truncated lists say so", "[gui][connections]") {
    ConnectionsPage page([] { return listOf({listenerOn(v4(0, 0, 0, 0), 22)}, 25); });
    page.refreshSync();
    REQUIRE(page.statusText().contains("+25 more not shown"));
}

TEST_CASE("there is no Close connection action on Linux", "[gui][connections]") {
    ConnectionsPage page([] { return ConnectionList{}; });
    for (auto* b : page.findChildren<QPushButton*>()) REQUIRE_FALSE(b->text().contains("Close"));
}

TEST_CASE("host names are looked up only when switched on", "[gui][connections]") {
    std::atomic<int> lookups{0};
    ConnectionsPage page([] { return listOf({conn(5000, v4(1, 2, 3, 4), 443)}); });
    page.setResolver([&](const IpAddr&) {
        ++lookups;
        return std::string("example.org");
    });
    page.refreshSync();
    REQUIRE(page.model()->rowAt(0).host.empty());
    waitFor([] { return false; }, 100);
    REQUIRE(lookups.load() == 0);  // nothing leaves the machine by default
    page.setResolveHosts(true);
    REQUIRE(waitFor([&] { return page.model()->rowAt(0).host == "example.org"; }));
    REQUIRE(lookups.load() == 1);
    page.refreshSync();  // cached: no second lookup
    waitFor([] { return false; }, 100);
    REQUIRE(lookups.load() == 1);
    page.setResolveHosts(false);
    REQUIRE(page.model()->rowAt(0).host.empty());
}

TEST_CASE("a failed lookup shows a dash and is not repeated", "[gui][connections]") {
    std::atomic<int> lookups{0};
    ConnectionsPage page([] { return listOf({conn(5000, v4(1, 2, 3, 4), 443)}); });
    page.setResolver([&](const IpAddr&) {
        ++lookups;
        return std::string();
    });
    page.setResolveHosts(true);
    page.refreshSync();
    REQUIRE(waitFor([&] { return page.model()->rowAt(0).host == "—"; }));
    page.refreshSync();
    waitFor([] { return false; }, 100);
    REQUIRE(lookups.load() == 1);
}

TEST_CASE("the main window opens the Connections page and navigates to a process", "[gui][connections]") {
    MainWindow w;
    w.showPage(Page::Connections);
    REQUIRE(dynamic_cast<ConnectionsPage*>(w.pageWidget(Page::Connections)) != nullptr);
    REQUIRE(w.currentPage() == Page::Connections);
}


TEST_CASE("looked-up names expire: found names after 10 minutes, failures after 5", "[gui][connections][minor]") {
    std::atomic<int> lookups{0};
    std::atomic<bool> found{true};
    auto fake = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::time_point{} + std::chrono::hours(1));
    ConnectionsPage page([] { return listOf({conn(5000, v4(1, 2, 3, 4), 443)}); });
    page.setClock([fake] { return *fake; });
    page.setResolver([&](const IpAddr&) {
        ++lookups;
        return found ? std::string("example.org") : std::string();
    });
    page.setResolveHosts(true);
    page.refreshSync();
    REQUIRE(waitFor([&] { return page.model()->rowAt(0).host == "example.org"; }));
    REQUIRE(lookups.load() == 1);
    *fake += std::chrono::minutes(9);
    page.refreshSync();
    waitFor([] { return false; }, 100);
    REQUIRE(lookups.load() == 1);  // still trusted
    *fake += std::chrono::minutes(2);  // 11 minutes in
    found = false;
    page.refreshSync();
    REQUIRE(waitFor([&] { return lookups.load() == 2; }));  // asked again
    REQUIRE(waitFor([&] { return page.model()->rowAt(0).host == "—"; }));
    *fake += std::chrono::minutes(4);
    page.refreshSync();
    waitFor([] { return false; }, 100);
    REQUIRE(lookups.load() == 2);  // a failure is remembered for 5 minutes
    *fake += std::chrono::minutes(2);
    page.refreshSync();
    REQUIRE(waitFor([&] { return lookups.load() == 3; }));
}
