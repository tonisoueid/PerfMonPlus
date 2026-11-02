// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/command.hpp>
#include <perfmonplus/linux/process_actions.hpp>

#include <QCheckBox>
#include <QDialog>
#include <QMessageBox>
#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pmp::gui {

// What the UI asks of the operating system. The real one wraps linux::ProcessActions; tests inject a fake.
class ProcessActionPort {
public:
    virtual ~ProcessActionPort() = default;
    virtual linux::ActionResult signal(const linux::ProcessTarget&, linux::Signal) = 0;
    virtual linux::ActionResult signalTree(const std::vector<linux::ProcessTarget>& leavesFirst, linux::Signal) = 0;
    virtual linux::ActionResult setPriority(const linux::ProcessTarget&, int nice) = 0;
    virtual linux::ActionResult setAffinity(const linux::ProcessTarget&, const std::vector<int>& cpus) = 0;
    virtual std::optional<std::vector<int>> affinityOf(ProcessKey) = 0;
};
std::shared_ptr<ProcessActionPort> makeRealActionPort();

// Choose which CPUs a process may run on: one check box per logical CPU.
class AffinityDialog : public QDialog {
    Q_OBJECT
public:
    AffinityDialog(int logicalCpus, const std::vector<int>& current, QWidget* parent = nullptr);  // CPUs 0..n-1
    // The machine's real CPU ids: after a hot-unplug they are not 0..n-1.
    AffinityDialog(const std::vector<int>& cpuIds, const std::vector<int>& current, QWidget* parent = nullptr);
    std::vector<int> selected() const;
    int boxCount() const { return static_cast<int>(boxes_.size()); }

private:
    std::vector<QCheckBox*> boxes_;
    std::vector<int> ids_;
};

// The Processes page's actions (PRC-13…16). Every destructive one goes through the port, which re-verifies the
// process's identity first; the controller adds the confirmation rules (PRC-14) and the "did it really end?"
// follow-up for a graceful End task.
class ProcessActionsController : public QObject {
    Q_OBJECT
public:
    ProcessActionsController(std::shared_ptr<ProcessActionPort> port, QWidget* dialogParent);

    void setPort(std::shared_ptr<ProcessActionPort> port) { port_ = std::move(port); }
    void setGraceMilliseconds(int ms) { graceMs_ = ms; }
    void setConfirmHandler(std::function<bool(const QString& title, const QString& text)> handler) { confirm_ = std::move(handler); }
    void setCommandRunner(linux::CommandRunner runner) { run_ = std::move(runner); }
    void setUrlOpener(std::function<void(const QUrl&)> opener) { open_ = std::move(opener); }
    void setCurrentUid(unsigned uid) { uid_ = uid; }

    // The latest sample, so a graceful end can tell whether the process really went away.
    void frameUpdated(const Frame& f);

    static bool isKernelThread(const ProcessRecord& r);
    // Ours, but ending or freezing it takes the desktop session with it (shell, compositor, session manager...).
    static bool isSessionCritical(const ProcessRecord& r);
    // Names are shown as plain text, and Enter must never be the way a kill is confirmed.
    static QMessageBox* makeConfirmBox(const QString& title, const QString& text, QWidget* parent);
    static QDialog* makePriorityDialog(const ProcessRecord& r, QWidget* parent);
    static int clampNice(int nice) { return nice < -20 ? -20 : nice > 19 ? 19 : nice; }
    bool needsConfirmation(const ProcessRecord& r) const;

    void endTask(const ProcessRecord& r, bool viaKeyboard);
    void forceQuit(const ProcessRecord& r);
    void endTree(const ProcessRecord& root, const std::vector<ProcessRecord>& descendantsLeavesFirst);
    void suspend(const ProcessRecord& r);
    void resume(const ProcessRecord& r);
    void setPriorityValue(const ProcessRecord& r, int nice);
    void setAffinityValue(const ProcessRecord& r, const std::vector<int>& cpus);
    void choosePriority(const ProcessRecord& r);                   // dialogs
    void chooseAffinity(const ProcessRecord& r, const std::vector<int>& cpuIds);
    void openLocation(const ProcessRecord& r);
    void copyRow(const QString& tabSeparated);
    void copyPid(const ProcessRecord& r);
    void copyPath(const ProcessRecord& r);
    void searchOnline(const ProcessRecord& r);

signals:
    void outcome(QString message, bool ok);        // shown in the page's message bar
    void forceQuitOffered(pmp::ProcessKey key, QString name);

private:
    struct Pending {
        ProcessKey key;
        QString name;
    };
    linux::ProcessTarget target(const ProcessRecord& r) const;
    bool confirm(const QString& title, const QString& text);
    void report(const linux::ActionResult& r);
    bool stillRunning(ProcessKey key) const;

    std::shared_ptr<ProcessActionPort> port_;
    QWidget* parent_;
    int graceMs_ = 5000;
    unsigned uid_;
    std::function<bool(const QString&, const QString&)> confirm_;
    linux::CommandRunner run_;
    std::function<void(const QUrl&)> open_;
    std::vector<ProcessKey> live_;  // sorted: the processes running in the latest sample
    bool haveFrame_ = false;
};

}  // namespace pmp::gui
