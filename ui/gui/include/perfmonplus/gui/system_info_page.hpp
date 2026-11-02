// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/vm/system_model.hpp>

#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QStringList>
#include <QTableWidget>
#include <QWidget>

namespace pmp::gui {

// A categorised, read-only inventory (§7.4). Serial numbers and MAC addresses stay hidden, in the table and in
// both exports, until "Include identifiers" is ticked.
class SystemInfoPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit SystemInfoPage(QWidget* parent = nullptr);

    void setInfo(const linux::SystemInfo& info);
    void setSystemInfo(const linux::SystemInfo& info) override { setInfo(info); }
    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }

    QStringList categories() const;
    void selectCategory(const QString& name);
    QTableWidget* table() const { return table_; }
    QCheckBox* includeIdentifiersCheck() const { return includeIdentifiers_; }
    QString exportText() const;
    // Writes the export; on failure says so (the page shows it under the buttons) instead of failing silently.
    bool saveTo(const QString& path, bool json);
    QString exportStatus() const { return exportStatus_->text(); }
    QString exportJson() const;

private:
    std::vector<vm::InfoSection> sections() const;
    void rebuild();

    linux::SystemInfo info_;
    std::vector<ProviderHealth> health_;
    QListWidget* list_;
    QTableWidget* table_;
    QCheckBox* includeIdentifiers_;
    QLabel* exportStatus_;
};

}  // namespace pmp::gui
