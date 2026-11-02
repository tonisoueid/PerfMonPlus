// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/linux/services.hpp>
#include <perfmonplus/vm/tables_model.hpp>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace pmp::gui {

// systemd services (§7.7): list, filter, start / stop / restart, logs. Everything that can block runs off the UI
// thread, and privileged actions are authorised by polkit through systemctl itself.
class ServicesPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit ServicesPage(std::shared_ptr<const linux::ServicesCollector> collector, QWidget* parent = nullptr);

    void refresh();                                    // async; emits refreshed() when the table is up to date
    void runAction(const QString& action, int row);    // start | stop | restart on the unit in `row`
    void setConfirmActions(bool on) { confirm_ = on; }
    QTableWidget* table() const { return table_; }
    QLineEdit* filterField() const { return filter_; }
    QString lastError() const { return actionError_.isEmpty() ? listError_ : actionError_; }

    void update(const Frame&, const HistoryStore&, MonoTime) override {}
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }

signals:
    void refreshed();

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    struct Result {
        std::vector<linux::ServiceInfo> services;
        QString error;
    };
    linux::ServiceScope scope() const;
    void fill();
    void showLogs();

    std::shared_ptr<const linux::ServicesCollector> collector_;
    std::vector<linux::ServiceInfo> services_;
    QLineEdit* filter_;
    QComboBox* status_;
    QComboBox* scope_;
    QLabel* error_;
    QTableWidget* table_;
    QTimer timer_;
    QString listError_, actionError_;
    bool confirm_ = true;
    QString selectedUnit_;  // survives refreshes: the table's row numbers do not
    bool refreshing_ = false;
    bool refreshAgain_ = false;
};

}  // namespace pmp::gui
