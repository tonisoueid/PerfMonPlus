// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/vm/process_table.hpp>

#include <QAbstractTableModel>

#include <unordered_map>
#include <vector>

namespace pmp::gui {

// The Processes table's data: a flat, ordered list of rows built by the view model. Updating it keeps the selection
// and scroll position (PRC-05): when the same rows arrive in the same order only the values change; when rows are
// added, removed or reordered, the views' persistent indexes are moved to wherever each row id went.
class ProcessTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,  // std::string as QString
        HeatRole,                   // 0..1, or -1 for no bar
        TypeRole,                   // vm::TableRow::Type as int
        DepthRole,
        ExpandableRole,
        ExpandedRole,
        TombstoneRole,
        PinnedRole,
        PidRole,
        StartRole
    };

    explicit ProcessTableModel(QObject* parent = nullptr);

    void setColumns(std::vector<vm::ProcessColumn> columns);
    const std::vector<vm::ProcessColumn>& columns() const { return columns_; }
    void setRows(std::vector<vm::TableRow> rows);

    const vm::TableRow& rowAt(int row) const { return rows_.at(static_cast<std::size_t>(row)); }
    int rowOfId(const std::string& id) const;  // -1 when absent
    int layoutChangeCount() const { return layoutChanges_; }  // tests: updates that moved rows
    int dataChangeCount() const { return dataChanges_; }      // tests: updates that only changed values

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(columns_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

signals:
    void structureChanged();  // rows were added, removed or reordered, or the columns changed

private:
    std::vector<vm::ProcessColumn> columns_{vm::ProcessColumn::Name};
    std::vector<vm::TableRow> rows_;
    std::unordered_map<std::string, int> rowById_;
    int layoutChanges_ = 0, dataChanges_ = 0;
};

}  // namespace pmp::gui
