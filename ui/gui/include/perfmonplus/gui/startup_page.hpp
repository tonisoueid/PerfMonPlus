// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/linux/startup.hpp>
#include <perfmonplus/vm/tables_model.hpp>

#include <QLabel>
#include <QTableWidget>
#include <QWidget>

#include <memory>

namespace pmp::gui {

// Autostart entries and enabled systemd user services (§7.5). Disabling a system entry writes a per-user override,
// so nothing here needs elevation.
class StartupPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit StartupPage(std::shared_ptr<const linux::StartupCollector> collector, QWidget* parent = nullptr);

    void refresh();
    void setEnabledAt(int row, bool enabled);
    QTableWidget* table() const { return table_; }
    QString lastError() const { return error_->text(); }

    void update(const Frame&, const HistoryStore&, MonoTime) override {}
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }

signals:
    void refreshed();

protected:
    void showEvent(QShowEvent* e) override;

private:
    struct Result {
        std::vector<linux::StartupEntry> entries;
        QString error;
    };
    std::shared_ptr<const linux::StartupCollector> collector_;
    std::vector<linux::StartupEntry> entries_;
    QTableWidget* table_;
    QLabel* error_;
};

}  // namespace pmp::gui
