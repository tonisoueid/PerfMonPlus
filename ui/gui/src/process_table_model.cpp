// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/process_table_model.hpp>

#include <QString>

#include <algorithm>

namespace pmp::gui {

ProcessTableModel::ProcessTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void ProcessTableModel::setColumns(std::vector<vm::ProcessColumn> columns) {
    beginResetModel();
    columns_ = std::move(columns);
    endResetModel();
    emit structureChanged();
}

int ProcessTableModel::rowOfId(const std::string& id) const {
    const auto it = rowById_.find(id);
    return it == rowById_.end() ? -1 : it->second;
}

void ProcessTableModel::setRows(std::vector<vm::TableRow> rows) {
    bool sameShape = rows.size() == rows_.size();
    for (std::size_t i = 0; sameShape && i < rows.size(); ++i) {
        sameShape = rows[i].id == rows_[i].id && rows[i].expanded == rows_[i].expanded &&
                    rows[i].depth == rows_[i].depth && rows[i].type == rows_[i].type;
    }
    if (sameShape) {
        rows_ = std::move(rows);
        ++dataChanges_;
        if (!rows_.empty()) emit dataChanged(index(0, 0), index(static_cast<int>(rows_.size()) - 1, columnCount() - 1));
        return;
    }

    emit layoutAboutToBeChanged();
    const auto oldIndexes = persistentIndexList();
    std::vector<std::string> oldIds;
    std::vector<std::vector<ProcessKey>> oldKeys;  // the processes each old row stood for
    for (const auto& idx : oldIndexes) {
        const bool valid = idx.isValid() && idx.row() < static_cast<int>(rows_.size());
        oldIds.push_back(valid ? rows_[static_cast<std::size_t>(idx.row())].id : std::string());
        std::vector<ProcessKey> keys;
        if (valid) {
            const auto& old = rows_[static_cast<std::size_t>(idx.row())];
            if (old.type == vm::TableRow::Type::Group) keys = old.members;
            else if (old.type == vm::TableRow::Type::Process) keys = {old.key};
        }
        oldKeys.push_back(std::move(keys));
    }
    rows_ = std::move(rows);
    rowById_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) rowById_[rows_[i].id] = static_cast<int>(i);
    QModelIndexList newIndexes;
    for (qsizetype k = 0; k < oldIndexes.size(); ++k) {
        const auto& oldId = oldIds[static_cast<std::size_t>(k)];
        int row = oldId.empty() ? -1 : rowOfId(oldId);
        if (row < 0) {
            // The row's id is gone because the process changed shape: a lone app process became a group (or the other
            // way round). Follow the process to whichever row now stands for it.
            const auto& keys = oldKeys[static_cast<std::size_t>(k)];
            for (std::size_t i = 0; i < rows_.size() && row < 0 && !keys.empty(); ++i) {
                const auto& now = rows_[i];
                const auto has = [&](ProcessKey key) { return std::find(keys.begin(), keys.end(), key) != keys.end(); };
                if (now.type == vm::TableRow::Type::Process && has(now.key)) row = static_cast<int>(i);
                else if (now.type == vm::TableRow::Type::Group && std::any_of(now.members.begin(), now.members.end(), has)) row = static_cast<int>(i);
            }
        }
        newIndexes << (row < 0 ? QModelIndex() : index(row, oldIndexes[k].column()));
    }
    changePersistentIndexList(oldIndexes, newIndexes);
    ++layoutChanges_;
    emit layoutChanged();
    emit structureChanged();
}

QVariant ProcessTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size()) || index.column() >= static_cast<int>(columns_.size())) return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    const auto col = columns_[static_cast<std::size_t>(index.column())];
    switch (role) {
        case Qt::DisplayRole:
            if (row.type == vm::TableRow::Type::Header) return index.column() == 0 ? QString::fromStdString(row.title) : QString();
            return QString::fromStdString(row.cells[static_cast<std::size_t>(col)]);
        case Qt::TextAlignmentRole:
            return static_cast<int>((vm::columnInfo(col).numeric ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
        case Qt::ToolTipRole:
            // Only text we wrote ourselves: names and paths come from other programs and could be taken for markup.
            return row.tombstone ? QVariant(QString::fromStdString(row.tooltip)) : QVariant();
        case IdRole: return QString::fromStdString(row.id);
        case HeatRole: return row.heat[static_cast<std::size_t>(col)];
        case TypeRole: return static_cast<int>(row.type);
        case DepthRole: return row.depth;
        case ExpandableRole: return row.expandable;
        case ExpandedRole: return row.expanded;
        case TombstoneRole: return row.tombstone;
        case PinnedRole: return row.pinned;
        case PidRole: return row.key.pid;
        case StartRole: return QVariant::fromValue<qulonglong>(row.key.startTicks);
        default: return {};
    }
}

QVariant ProcessTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || section < 0 || section >= static_cast<int>(columns_.size())) return {};
    const auto& info = vm::columnInfo(columns_[static_cast<std::size_t>(section)]);
    if (role == Qt::DisplayRole) return QString::fromUtf8(info.title);
    if (role == Qt::ToolTipRole && info.tooltip[0] != '\0') return QString::fromUtf8(info.tooltip);
    if (role == Qt::TextAlignmentRole) return static_cast<int>((info.numeric ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    return {};
}

Qt::ItemFlags ProcessTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return Qt::NoItemFlags;
    if (rows_[static_cast<std::size_t>(index.row())].type == vm::TableRow::Type::Header) return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

}  // namespace pmp::gui
