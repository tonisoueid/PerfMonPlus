// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/linux/sessions.hpp>
#include <perfmonplus/vm/tables_model.hpp>

#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace pmp::gui {

// One row per logged-in user, with that user's CPU / memory / disk summed from the live process list (§7.6).
class UsersPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit UsersPage(std::shared_ptr<const linux::SessionsCollector> collector, QWidget* parent = nullptr);

    void refresh();
    void lockSession(int row);
    void signOff(int row);
    void setConfirmActions(bool on) { confirm_ = on; }
    QTableWidget* table() const { return table_; }
    QString lastError() const { return error_->text(); }  // list error and action error together

    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }

signals:
    void refreshed();
    void userProcessesRequested(const QString& user);  // double click: show this user's processes

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    struct Result {
        std::vector<linux::SessionInfo> sessions;
        QString error;
    };
    void fill();
    void showErrors();
    void act(int row, bool terminate);

    std::shared_ptr<const linux::SessionsCollector> collector_;
    std::vector<linux::SessionInfo> sessions_;
    std::vector<vm::UserRow> rows_;
    Frame frame_;
    QTableWidget* table_;
    QLabel* error_;
    QString listError_, actionError_, selectedUser_;  // the selection is kept by user name, not by row
    QTimer timer_;
    bool confirm_ = true;
};

}  // namespace pmp::gui
