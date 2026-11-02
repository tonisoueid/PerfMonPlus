// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/vm/process_details.hpp>

#include <QFrame>
#include <QLabel>
#include <QTableWidget>

#include <vector>

namespace pmp::gui {

// "Which processes caused this?" (HIS-03): a small popup listing the processes that used a resource during a
// selected stretch of the graph, including ones that have since exited. It shows a snapshot: later samples do
// not change it.
class CulpritsPopover : public QFrame {
    Q_OBJECT
public:
    explicit CulpritsPopover(QWidget* parent = nullptr);

    void present(const std::vector<vm::CulpritRow>& rows, const QString& title, QPoint at);
    void presentMessage(const QString& message, const QString& title, QPoint at);
    QTableWidget* table() const { return table_; }
    QString title() const { return title_->text(); }
    QString message() const { return message_->isHidden() ? QString() : message_->text(); }
    void goTo(int row);  // what the row's "Go to process" button does

signals:
    void processRequested(pmp::ProcessKey key);

private:
    QLabel* title_;
    QLabel* message_;
    QTableWidget* table_;
    std::vector<ProcessKey> keys_;
};

// Connects selectable graphs to a popover: when a range is selected it asks the per-process history who was busy
// then. Shared by the Summary and Performance pages.
class CulpritsPresenter : public QObject {
    Q_OBJECT
public:
    explicit CulpritsPresenter(QWidget* parent);

    void setHistory(const ProcessHistory* history) { history_ = history; }
    const ProcessHistory* history() const { return history_; }  // null: per-process history is switched off
    void setLogicalCpus(int n) { cpus_ = n; }
    void attach(HistoryGraph* graph, ProcessHistory::Resource resource);
    void show(ProcessHistory::Resource resource, MonoTime from, MonoTime to, QPoint at);  // also used by tests
    CulpritsPopover* popover() const { return popover_; }

signals:
    void processRequested(pmp::ProcessKey key);

private:
    const ProcessHistory* history_ = nullptr;
    int cpus_ = 1;
    CulpritsPopover* popover_;
};

}  // namespace pmp::gui
