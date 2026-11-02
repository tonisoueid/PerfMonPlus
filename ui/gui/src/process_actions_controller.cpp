// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/process_actions_controller.hpp>

#include <perfmonplus/gui/async.hpp>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QUrlQuery>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <memory>

namespace pmp::gui {
namespace {

class RealPort final : public ProcessActionPort {
public:
    linux::ActionResult signal(const linux::ProcessTarget& t, linux::Signal s) override { return actions_.signal(t, s); }
    linux::ActionResult signalTree(const std::vector<linux::ProcessTarget>& t, linux::Signal s) override { return actions_.signalTree(t, s); }
    linux::ActionResult setPriority(const linux::ProcessTarget& t, int nice) override { return actions_.setPriority(t, nice); }
    linux::ActionResult setAffinity(const linux::ProcessTarget& t, const std::vector<int>& cpus) override { return actions_.setAffinity(t, cpus); }
    std::optional<std::vector<int>> affinityOf(ProcessKey key) override { return actions_.affinityOf(key); }

private:
    linux::ProcessActions actions_;
};

QString q(const std::string& s) { return QString::fromStdString(s); }

}  // namespace

std::shared_ptr<ProcessActionPort> makeRealActionPort() { return std::make_shared<RealPort>(); }

AffinityDialog::AffinityDialog(int logicalCpus, const std::vector<int>& current, QWidget* parent)
    : AffinityDialog([&] {
          std::vector<int> ids(static_cast<std::size_t>(std::max(0, logicalCpus)));
          for (int i = 0; i < logicalCpus; ++i) ids[static_cast<std::size_t>(i)] = i;
          return ids;
      }(),
                     current, parent) {}

AffinityDialog::AffinityDialog(const std::vector<int>& cpuIds, const std::vector<int>& current, QWidget* parent) : QDialog(parent), ids_(cpuIds) {
    setWindowTitle("Set CPU affinity");
    auto* col = new QVBoxLayout(this);
    col->addWidget(new QLabel("The process may run on these CPUs:", this));
    for (const int id : ids_) {
        auto* box = new QCheckBox(QString("CPU %1").arg(id), this);
        box->setChecked(std::find(current.begin(), current.end(), id) != current.end() || current.empty());
        boxes_.push_back(box);
        col->addWidget(box);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);
}

std::vector<int> AffinityDialog::selected() const {
    std::vector<int> cpus;
    for (std::size_t i = 0; i < boxes_.size(); ++i) {
        if (boxes_[i]->isChecked()) cpus.push_back(ids_[i]);
    }
    return cpus;
}

ProcessActionsController::ProcessActionsController(std::shared_ptr<ProcessActionPort> port, QWidget* dialogParent)
    : QObject(dialogParent),
      port_(std::move(port)),
      parent_(dialogParent),
      uid_(static_cast<unsigned>(::geteuid())),
      run_([](const std::vector<std::string>& argv) { return linux::runDefault(argv); }),
      open_([](const QUrl& url) { QDesktopServices::openUrl(url); }) {}

void ProcessActionsController::frameUpdated(const Frame& f) {
    live_.clear();
    for (const auto& r : f.processes.processes) {
        if (!r.exitedAt) live_.push_back(r.key);
    }
    std::sort(live_.begin(), live_.end());
    haveFrame_ = true;
}

bool ProcessActionsController::isKernelThread(const ProcessRecord& r) {
    return r.kernelThread;
}

bool ProcessActionsController::isSessionCritical(const ProcessRecord& r) {
    static const char* const names[] = {"gnome-shell",   "gnome-session-b", "Xwayland",   "Xorg",        "kwin_wayland", "kwin_x11",
                                        "plasmashell",   "sway",            "Hyprland",   "dbus-daemon", "pipewire",     "wireplumber",
                                        "pulseaudio",    "systemd",         "gdm-wayland-ses", "gdm-session-wor", "sddm", "lightdm"};
    for (const char* n : names) {
        if (r.name == n) return true;
    }
    return false;
}

bool ProcessActionsController::needsConfirmation(const ProcessRecord& r) const {
    return r.kind == ProcessKind::System || r.uid == 0 || r.uid != uid_ || isSessionCritical(r);
}

linux::ProcessTarget ProcessActionsController::target(const ProcessRecord& r) const {
    return linux::ProcessTarget{r.key, r.name, isKernelThread(r)};
}

QMessageBox* ProcessActionsController::makeConfirmBox(const QString& title, const QString& text, QWidget* parent) {
    auto* box = new QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
    box->setTextFormat(Qt::PlainText);
    box->setDefaultButton(QMessageBox::No);
    box->setEscapeButton(QMessageBox::No);
    return box;
}

bool ProcessActionsController::confirm(const QString& title, const QString& text) {
    if (confirm_) return confirm_(title, text);
    std::unique_ptr<QMessageBox> box(makeConfirmBox(title, text, parent_));
    return box->exec() == QMessageBox::Yes;
}

void ProcessActionsController::report(const linux::ActionResult& r) { emit outcome(q(r.message), r.ok); }

bool ProcessActionsController::stillRunning(ProcessKey key) const {
    if (!haveFrame_) return true;  // no news is not proof of exit
    return std::binary_search(live_.begin(), live_.end(), key);
}

void ProcessActionsController::endTask(const ProcessRecord& r, bool viaKeyboard) {
    const QString name = q(r.name);
    if (viaKeyboard || needsConfirmation(r)) {
        const QString why = isSessionCritical(r) && r.kind != ProcessKind::System && r.uid == uid_
                                ? QString("%1 is part of your desktop session. Ending it may end the session and lose unsaved work.").arg(name)
                            : r.kind == ProcessKind::System || r.uid == 0
                                ? QString("%1 is a system process. Ending it may make the system unstable or end your session.").arg(name)
                                : r.uid != uid_ ? QString("%1 belongs to another user.").arg(name) : QString("Unsaved work in %1 will be lost.").arg(name);
        if (!confirm("End task", QString("End %1 (PID %2)? %3").arg(name).arg(r.key.pid).arg(why))) return;
    }
    const auto result = port_->signal(target(r), linux::Signal::Term);
    report(result);
    if (!result.ok) return;
    // A polite request can be ignored: check back, and offer to force it.
    const ProcessKey key = r.key;
    QTimer::singleShot(graceMs_, this, [this, key, name] {
        if (stillRunning(key)) emit forceQuitOffered(key, name);
    });
}

void ProcessActionsController::forceQuit(const ProcessRecord& r) { report(port_->signal(target(r), linux::Signal::Kill)); }

void ProcessActionsController::endTree(const ProcessRecord& root, const std::vector<ProcessRecord>& descendants) {
    const QString name = q(root.name);
    const std::size_t total = descendants.size() + 1;
    if (!confirm("End process tree", QString("End %1 (PID %2) and its %3 child process(es)? Unsaved work in any of them will be lost.")
                                         .arg(name).arg(root.key.pid).arg(total - 1))) {
        return;
    }
    std::vector<linux::ProcessTarget> targets;
    for (const auto& d : descendants) targets.push_back(target(d));
    targets.push_back(target(root));
    report(port_->signalTree(targets, linux::Signal::Term));
}

void ProcessActionsController::suspend(const ProcessRecord& r) {
    if (isSessionCritical(r) &&
        !confirm("Suspend", QString("Suspend %1 (PID %2)? It is part of your desktop session: freezing it can freeze the screen, "
                                    "and you would have nothing to click Resume with.")
                                .arg(QString::fromStdString(r.name))
                                .arg(r.key.pid))) {
        return;
    }
    report(port_->signal(target(r), linux::Signal::Stop));
}
void ProcessActionsController::resume(const ProcessRecord& r) { report(port_->signal(target(r), linux::Signal::Cont)); }

void ProcessActionsController::setPriorityValue(const ProcessRecord& r, int nice) {
    report(port_->setPriority(target(r), clampNice(nice)));
}

void ProcessActionsController::setAffinityValue(const ProcessRecord& r, const std::vector<int>& cpus) {
    report(port_->setAffinity(target(r), cpus));
}

QDialog* ProcessActionsController::makePriorityDialog(const ProcessRecord& r, QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setWindowTitle("Set priority");
    auto* col = new QVBoxLayout(dialog);
    auto* prompt = new QLabel(QString("Nice value for %1 (PID %2):").arg(q(r.name)).arg(r.key.pid), dialog);
    prompt->setTextFormat(Qt::PlainText);  // the name comes from another program
    col->addWidget(prompt);
    auto* spin = new QSpinBox(dialog);
    spin->setRange(-20, 19);
    spin->setValue(r.priority.isMeasured() ? clampNice(static_cast<int>(*r.priority.value)) : 0);
    col->addWidget(spin);
    auto* note = new QLabel("Higher numbers run less often. Lowering the value below its current setting needs privileges PerfMonPlus does not have.", dialog);
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    col->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    col->addWidget(buttons);
    return dialog;
}

void ProcessActionsController::choosePriority(const ProcessRecord& r) {
    std::unique_ptr<QDialog> dialog(makePriorityDialog(r, parent_));
    if (dialog->exec() == QDialog::Accepted) setPriorityValue(r, dialog->findChild<QSpinBox*>()->value());
}

void ProcessActionsController::chooseAffinity(const ProcessRecord& r, const std::vector<int>& cpuIds) {
    const auto current = port_->affinityOf(r.key).value_or(std::vector<int>{});
    AffinityDialog dialog(cpuIds, current, parent_);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto cpus = dialog.selected();
    if (cpus.empty()) {
        emit outcome("Choose at least one CPU.", false);
        return;
    }
    setAffinityValue(r, cpus);
}

void ProcessActionsController::openLocation(const ProcessRecord& r) {
    if (r.exe.empty()) {
        emit outcome("This process has no known executable location.", false);
        return;
    }
    const std::string dir = std::filesystem::path(r.exe).parent_path().string();
    const auto run = run_;
    runAsync<linux::CommandResult>(
        this, [run, dir] { return run({"xdg-open", dir}); },
        [this](linux::CommandResult res) {
            if (res.exitCode != 0) emit outcome("Could not open the folder: " + q(linux::describeFailure(res)), false);
        });
}

void ProcessActionsController::copyRow(const QString& text) { QApplication::clipboard()->setText(text); }
void ProcessActionsController::copyPid(const ProcessRecord& r) { QApplication::clipboard()->setText(QString::number(r.key.pid)); }
void ProcessActionsController::copyPath(const ProcessRecord& r) { QApplication::clipboard()->setText(q(r.exe)); }

void ProcessActionsController::searchOnline(const ProcessRecord& r) {
    QUrl url("https://duckduckgo.com/");
    QUrlQuery query;
    query.addQueryItem("q", q(r.name));
    url.setQuery(query);
    open_(url);  // only on the user's click: nothing is sent from here
}

}  // namespace pmp::gui
