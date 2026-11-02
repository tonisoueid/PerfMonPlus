// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/gui/process_actions_controller.hpp>
#include <perfmonplus/gui/processes_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QTimer>
#include <QToolButton>

#include <unistd.h>

#include <chrono>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

MonoTime at(int s) { return MonoTime{} + std::chrono::seconds(s); }

ProcessRecord rec(int pid, std::string name, ProcessKind kind = ProcessKind::Background) {
    ProcessRecord r;
    r.key = {pid, static_cast<std::uint64_t>(pid) * 10};
    r.name = std::move(name);
    r.user = "dave";
    r.uid = static_cast<std::uint32_t>(::geteuid());  // ours: an ordinary process
    r.state = 'S';
    r.kind = kind;
    r.cpu = Metric::measured(1, Unit::Percent);
    r.memFootprint = Metric::measured(1000, Unit::Bytes);
    r.memPrivate = Metric::measured(500, Unit::Bytes);
    r.threads = Metric::measured(1, Unit::Count);
    r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
    r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
    r.gpu = Metric::measured(0, Unit::Percent);
    r.exe = "/usr/lib/firefox/firefox";
    return r;
}

Frame frameOf(std::vector<ProcessRecord> ps, int t = 100) {
    Frame f;
    f.mono = at(t);
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(10000, Unit::Bytes);
    f.processes.processes = std::move(ps);
    return f;
}

struct FakePort : ProcessActionPort {
    struct Call {
        std::string what;
        ProcessKey key;
        std::string name;
        int arg = 0;
        std::vector<int> cpus;
        std::vector<ProcessKey> tree;
    };
    std::vector<Call> calls;
    linux::ActionResult result{true, "ok"};

    linux::ActionResult signal(const linux::ProcessTarget& t, linux::Signal s) override {
        calls.push_back({"signal", t.key, t.name, static_cast<int>(s), {}, {}});
        return result;
    }
    linux::ActionResult signalTree(const std::vector<linux::ProcessTarget>& t, linux::Signal s) override {
        Call c{"tree", {}, {}, static_cast<int>(s), {}, {}};
        for (const auto& x : t) c.tree.push_back(x.key);
        calls.push_back(std::move(c));
        return result;
    }
    linux::ActionResult setPriority(const linux::ProcessTarget& t, int nice) override {
        calls.push_back({"nice", t.key, t.name, nice, {}, {}});
        return result;
    }
    linux::ActionResult setAffinity(const linux::ProcessTarget& t, const std::vector<int>& cpus) override {
        calls.push_back({"affinity", t.key, t.name, 0, cpus, {}});
        return result;
    }
    std::optional<std::vector<int>> affinityOf(ProcessKey) override { return std::vector<int>{0, 1}; }
};

struct Fixture {
    ProcessesPage page;
    HistoryStore history;
    std::shared_ptr<FakePort> port = std::make_shared<FakePort>();
    std::vector<QString> prompts;
    bool answer = true;
    Fixture() {
        page.resize(1000, 700);
        page.show();
        page.setView(vm::ProcessView::Flat);
        page.setActionPort(port);
        page.actions()->setConfirmHandler([this](const QString& title, const QString& text) {
            prompts.push_back(title + ": " + text);
            return answer;
        });
    }
    void feed(const Frame& f) { page.update(f, history, f.mono); }
};

void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void key(QWidget* w, Qt::Key k) {
    QKeyEvent ev(QEvent::KeyPress, k, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

}  // namespace

TEST_CASE("end task sends Term to the right key and name", "[gui][actions-ui]") {
    Fixture fx;
    fx.feed(frameOf({rec(7, "worker"), rec(8, "other")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    fx.page.endSelectedTask(false);
    REQUIRE(fx.port->calls.size() == 1);
    REQUIRE(fx.port->calls[0].what == "signal");
    REQUIRE(fx.port->calls[0].key == ProcessKey{7, 70});
    REQUIRE(fx.port->calls[0].name == "worker");
    REQUIRE(fx.port->calls[0].arg == static_cast<int>(linux::Signal::Term));
    REQUIRE(fx.prompts.empty());  // an ordinary process of ours is not confirmed
}

TEST_CASE("a system process is confirmed first", "[gui][actions-ui]") {
    Fixture fx;
    auto sys = rec(1, "systemd", ProcessKind::System);
    sys.uid = 0;
    fx.feed(frameOf({sys}));
    REQUIRE(fx.page.selectProcess(sys.key));
    fx.answer = false;
    fx.page.endSelectedTask(false);
    REQUIRE(fx.prompts.size() == 1);
    REQUIRE(fx.prompts[0].contains("system process"));
    REQUIRE(fx.port->calls.empty());  // declined: nothing was sent
    fx.answer = true;
    fx.page.endSelectedTask(false);
    REQUIRE(fx.port->calls.size() == 1);
}

TEST_CASE("the Delete key always confirms", "[gui][actions-ui]") {
    Fixture fx;
    fx.feed(frameOf({rec(7, "worker")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    key(fx.page.table(), Qt::Key_Delete);
    REQUIRE(fx.prompts.size() == 1);
    REQUIRE(fx.port->calls.size() == 1);
    fx.answer = false;
    key(fx.page.table(), Qt::Key_Delete);
    REQUIRE(fx.prompts.size() == 2);
    REQUIRE(fx.port->calls.size() == 1);
}

TEST_CASE("the exact recycled-pid message is shown", "[gui][actions-ui]") {
    Fixture fx;
    fx.port->result = {false, "worker (PID 7) has exited; PID 7 now belongs to a different process. No action taken."};
    fx.feed(frameOf({rec(7, "worker")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    std::vector<std::pair<QString, bool>> seen;
    QObject::connect(fx.page.actions(), &ProcessActionsController::outcome, &fx.page, [&](QString m, bool ok) { seen.push_back({m, ok}); });
    fx.page.endSelectedTask(false);
    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0].first == "worker (PID 7) has exited; PID 7 now belongs to a different process. No action taken.");
    REQUIRE_FALSE(seen[0].second);
    REQUIRE(fx.page.messageText() == seen[0].first);
}

TEST_CASE("force quit is offered when the process outlives the grace period", "[gui][actions-ui]") {
    Fixture fx;
    fx.page.actions()->setGraceMilliseconds(40);
    fx.feed(frameOf({rec(7, "stubborn")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    int offered = 0;
    QObject::connect(fx.page.actions(), &ProcessActionsController::forceQuitOffered, &fx.page, [&](ProcessKey, QString) { ++offered; });
    fx.page.endSelectedTask(false);
    fx.feed(frameOf({rec(7, "stubborn")}, 101));  // still running
    wait(150);
    REQUIRE(offered == 1);
    REQUIRE(fx.page.forceQuitBarVisible());
    fx.page.forceQuitButton()->click();
    REQUIRE(fx.port->calls.back().arg == static_cast<int>(linux::Signal::Kill));
    REQUIRE_FALSE(fx.page.forceQuitBarVisible());
}

TEST_CASE("force quit is not offered when the process exits in time", "[gui][actions-ui]") {
    Fixture fx;
    fx.page.actions()->setGraceMilliseconds(40);
    fx.feed(frameOf({rec(7, "polite")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    int offered = 0;
    QObject::connect(fx.page.actions(), &ProcessActionsController::forceQuitOffered, &fx.page, [&](ProcessKey, QString) { ++offered; });
    fx.page.endSelectedTask(false);
    auto gone = rec(7, "polite");
    gone.exitedAt = at(101);
    fx.feed(frameOf({gone}, 101));
    wait(150);
    REQUIRE(offered == 0);
}

TEST_CASE("end tree sends leaves first and always confirms", "[gui][actions-ui]") {
    Fixture fx;
    auto root = rec(10, "root");
    auto child = rec(11, "child");
    child.parent = root.key;
    auto grandchild = rec(12, "grandchild");
    grandchild.parent = child.key;
    fx.feed(frameOf({root, child, grandchild, rec(99, "unrelated")}));
    fx.page.setView(vm::ProcessView::Tree);
    fx.page.table()->setFocus();
    REQUIRE(fx.page.selectProcess(root.key));
    // the context-menu path: ask the controller with the page's own view of the tree
    const auto selected = fx.page.selectedKey();
    REQUIRE(selected == root.key);
    fx.page.actions()->endTree(root, {grandchild, child});
    REQUIRE(fx.prompts.size() == 1);
    REQUIRE(fx.prompts[0].contains("2 child process"));
    REQUIRE(fx.port->calls.size() == 1);
    REQUIRE(fx.port->calls[0].what == "tree");
    REQUIRE(fx.port->calls[0].tree == std::vector<ProcessKey>{grandchild.key, child.key, root.key});
}

TEST_CASE("suspend and resume send Stop and Cont", "[gui][actions-ui]") {
    Fixture fx;
    fx.feed(frameOf({rec(7, "worker")}));
    const auto r = rec(7, "worker");
    fx.page.actions()->suspend(r);
    fx.page.actions()->resume(r);
    REQUIRE(fx.port->calls[0].arg == static_cast<int>(linux::Signal::Stop));
    REQUIRE(fx.port->calls[1].arg == static_cast<int>(linux::Signal::Cont));
}

TEST_CASE("priority is clamped and affinity has one box per cpu", "[gui][actions-ui]") {
    Fixture fx;
    const auto r = rec(7, "worker");
    fx.page.actions()->setPriorityValue(r, 25);
    fx.page.actions()->setPriorityValue(r, -99);
    REQUIRE(fx.port->calls[0].arg == 19);
    REQUIRE(fx.port->calls[1].arg == -20);
    AffinityDialog dialog(4, {1, 2});
    REQUIRE(dialog.boxCount() == 4);
    REQUIRE(dialog.selected() == std::vector<int>{1, 2});
    AffinityDialog all(3, {});
    REQUIRE(all.selected() == std::vector<int>{0, 1, 2});
    fx.page.actions()->setAffinityValue(r, {0, 2});
    REQUIRE(fx.port->calls[2].cpus == std::vector<int>{0, 2});
}

TEST_CASE("kernel threads have destructive actions disabled", "[gui][actions-ui]") {
    Fixture fx;
    auto kthreadd = rec(2, "kthreadd", ProcessKind::System);
    auto worker = rec(20, "kworker/0:1", ProcessKind::System);
    worker.parent = kthreadd.key;
    kthreadd.kernelThread = worker.kernelThread = true;  // the kernel's own flag, as the provider sets it
    fx.feed(frameOf({kthreadd, worker, rec(7, "ordinary")}));
    const auto button = [&](const QString& text) {
        for (auto* b : fx.page.findChildren<QToolButton*>())
            if (b->text() == text) return b;
        return static_cast<QToolButton*>(nullptr);
    };
    REQUIRE(fx.page.selectProcess(worker.key));
    REQUIRE_FALSE(button("End task")->isEnabled());
    REQUIRE_FALSE(button("Suspend")->isEnabled());
    REQUIRE(fx.page.selectProcess({7, 70}));
    REQUIRE(button("End task")->isEnabled());
    fx.page.endSelectedTask(false);
    REQUIRE(ProcessActionsController::isKernelThread(worker));
    REQUIRE(ProcessActionsController::isKernelThread(kthreadd));
    REQUIRE_FALSE(ProcessActionsController::isKernelThread(rec(7, "ordinary")));
}

TEST_CASE("open location runs xdg-open with an argv vector", "[gui][actions-ui]") {
    Fixture fx;
    std::vector<std::string> argv;
    fx.page.actions()->setCommandRunner([&](const std::vector<std::string>& a) {
        argv = a;
        return linux::CommandResult{0, "", "", false};
    });
    fx.page.actions()->openLocation(rec(7, "firefox"));
    wait(200);
    REQUIRE(argv == std::vector<std::string>{"xdg-open", "/usr/lib/firefox"});

    std::vector<QString> messages;
    QObject::connect(fx.page.actions(), &ProcessActionsController::outcome, &fx.page, [&](QString m, bool) { messages.push_back(m); });
    auto nothing = rec(8, "kthread");
    nothing.exe.clear();
    argv.clear();
    fx.page.actions()->openLocation(nothing);
    wait(100);
    REQUIRE(argv.empty());  // disabled for an unknown path
    REQUIRE(messages.size() == 1);
}

TEST_CASE("copy row, pid and path put the right text on the clipboard", "[gui][actions-ui]") {
    Fixture fx;
    fx.page.actions()->copyPid(rec(4445, "firefox"));
    REQUIRE(QApplication::clipboard()->text() == "4445");
    fx.page.actions()->copyPath(rec(4445, "firefox"));
    REQUIRE(QApplication::clipboard()->text() == "/usr/lib/firefox/firefox");
    fx.page.actions()->copyRow("firefox\t4445");
    REQUIRE(QApplication::clipboard()->text() == "firefox\t4445");
}

TEST_CASE("search online builds an https url with the name percent-encoded", "[gui][actions-ui]") {
    Fixture fx;
    QUrl opened;
    fx.page.actions()->setUrlOpener([&](const QUrl& u) { opened = u; });
    fx.page.actions()->searchOnline(rec(1, "Fire fox&co"));
    REQUIRE(opened.scheme() == "https");
    REQUIRE(opened.host() == "duckduckgo.com");
    REQUIRE(QString(opened.toEncoded()) == "https://duckduckgo.com/?q=Fire%20fox%26co");
}

TEST_CASE("a failed action shows its message", "[gui][actions-ui]") {
    Fixture fx;
    fx.port->result = {false, "You do not have permission to end init (PID 1). It belongs to another user or to the system."};
    fx.feed(frameOf({rec(7, "worker")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    fx.page.endSelectedTask(false);
    REQUIRE(fx.page.messageText().startsWith("You do not have permission"));
}

TEST_CASE("type-ahead jumps to a name", "[gui][actions-ui]") {
    Fixture fx;
    fx.feed(frameOf({rec(1, "alpha"), rec(2, "firefox"), rec(3, "zsh")}));
    fx.page.sortBy(vm::ProcessColumn::Name);
    fx.page.table()->setCurrentIndex(fx.page.model()->index(0, 0));
    fx.page.table()->keyboardSearch("fire");
    REQUIRE(fx.page.selectedKey() == ProcessKey{2, 20});
}

TEST_CASE("a pinned tombstone stays after the provider drops it", "[gui][actions-ui]") {
    Fixture fx;
    auto gone = rec(7, "short-lived");
    gone.exitedAt = at(99);
    fx.feed(frameOf({rec(1, "live"), gone}, 100));
    fx.page.pinTombstone(gone.key);
    fx.feed(frameOf({rec(1, "live")}, 200));  // the provider has long forgotten it
    REQUIRE(fx.page.model()->rowOfId("p:7:70") >= 0);
    REQUIRE(fx.page.model()->data(fx.page.model()->index(fx.page.model()->rowOfId("p:7:70"), 0), ProcessTableModel::PinnedRole).toBool());
    fx.page.unpinTombstone(gone.key);
    REQUIRE(fx.page.model()->rowOfId("p:7:70") < 0);
}

TEST_CASE("a hostile name is never read as markup in a confirmation", "[gui][actions-ui]") {
    auto* box = ProcessActionsController::makeConfirmBox("End task", "End <b>x</b><a href=\"http://evil\">y</a> (PID 7)?", nullptr);
    REQUIRE(box->textFormat() == Qt::PlainText);
    REQUIRE(box->defaultButton() == box->button(QMessageBox::No));  // Enter does not confirm a kill
    delete box;
}

TEST_CASE("the priority dialog shows names as plain text", "[gui][actions-ui]") {
    auto r = rec(7, "<i>evil</i>");
    auto* dialog = ProcessActionsController::makePriorityDialog(r, nullptr);
    for (auto* label : dialog->findChildren<QLabel*>()) REQUIRE(label->textFormat() == Qt::PlainText);
    delete dialog;
}

TEST_CASE("session-critical processes are confirmed even though they are ours", "[gui][actions-ui]") {
    Fixture fx;
    auto shell = rec(30, "gnome-shell");  // ours, Background: one click would end the whole session
    fx.feed(frameOf({shell, rec(31, "ordinary")}));
    REQUIRE(ProcessActionsController::isSessionCritical(shell));
    REQUIRE_FALSE(ProcessActionsController::isSessionCritical(rec(31, "ordinary")));
    fx.answer = false;
    fx.page.actions()->endTask(shell, false);
    REQUIRE(fx.prompts.size() == 1);
    REQUIRE(fx.prompts[0].contains("session"));
    REQUIRE(fx.port->calls.empty());
    fx.page.actions()->suspend(shell);  // freezing the compositor leaves nothing to click Resume with
    REQUIRE(fx.prompts.size() == 2);
    REQUIRE(fx.port->calls.empty());
    fx.page.actions()->suspend(rec(31, "ordinary"));
    REQUIRE(fx.prompts.size() == 2);
    REQUIRE(fx.port->calls.size() == 1);
}

TEST_CASE("a pinned tombstone can still be selected and inspected after the provider drops it", "[gui][actions-ui]") {
    Fixture fx;
    auto gone = rec(7, "short-lived");
    gone.exitedAt = at(99);
    fx.feed(frameOf({rec(1, "live"), gone}, 100));
    fx.page.pinTombstone(gone.key);
    fx.feed(frameOf({rec(1, "live")}, 200));
    REQUIRE(fx.page.selectProcess(gone.key));
    REQUIRE(fx.page.details()->titleText() == "short-lived (7)");  // the pane has its record, not a blank
}

TEST_CASE("the affinity dialog uses the machine's real CPU ids", "[gui][actions-ui][minor]") {
    AffinityDialog dialog(std::vector<int>{0, 2, 5}, {2});  // CPUs 1, 3 and 4 are offline
    REQUIRE(dialog.boxCount() == 3);
    REQUIRE(dialog.selected() == std::vector<int>{2});
    REQUIRE(dialog.findChildren<QCheckBox*>()[2]->text() == "CPU 5");
    dialog.findChildren<QCheckBox*>()[2]->setChecked(true);
    REQUIRE(dialog.selected() == std::vector<int>{2, 5});  // ids, not positions
}

TEST_CASE("a force-quit offer does not outlive leaving the page", "[gui][actions-ui][minor]") {
    Fixture fx;
    fx.page.actions()->setGraceMilliseconds(40);
    fx.feed(frameOf({rec(7, "stubborn")}));
    REQUIRE(fx.page.selectProcess({7, 70}));
    fx.page.endSelectedTask(false);
    fx.feed(frameOf({rec(7, "stubborn")}, 101));
    wait(150);
    REQUIRE(fx.page.forceQuitBarVisible());
    fx.page.hide();
    fx.page.show();
    REQUIRE_FALSE(fx.page.forceQuitBarVisible());
}
