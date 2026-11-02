// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/startup_page.hpp>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>

namespace pmp::gui {

StartupPage::StartupPage(std::shared_ptr<const linux::StartupCollector> collector, QWidget* parent)
    : QWidget(parent), collector_(std::move(collector)), table_(new QTableWidget(0, 5, this)), error_(new QLabel(this)) {
    error_->setWordWrap(true);
    error_->hide();
    table_->setHorizontalHeaderLabels({"Name", "Status", "Startup impact", "Source", "Command"});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 280);
    table_->setColumnWidth(3, 170);

    auto* enable = new QPushButton("Enable", this);
    auto* disable = new QPushButton("Disable", this);
    auto* refreshBtn = new QPushButton("Refresh", this);
    auto* bar = new QHBoxLayout;
    bar->addWidget(enable);
    bar->addWidget(disable);
    bar->addStretch(1);
    bar->addWidget(refreshBtn);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    col->addWidget(error_);
    col->addWidget(table_, 1);
    col->addLayout(bar);
    connect(enable, &QPushButton::clicked, this, [this] { setEnabledAt(table_->currentRow(), true); });
    connect(disable, &QPushButton::clicked, this, [this] { setEnabledAt(table_->currentRow(), false); });
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
}

void StartupPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refresh();
}

void StartupPage::refresh() {
    const auto collector = collector_;
    runAsync<Result>(
        this,
        [collector] {
            Result r;
            try {
                r.entries = collector->collect();
            } catch (const std::exception& e) {
                r.error = QString::fromUtf8(e.what());
            }
            return r;
        },
        [this](Result r) {
            entries_ = std::move(r.entries);
            const auto rows = vm::startupRows(entries_);
            table_->setRowCount(static_cast<int>(rows.size()));
            for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
                const auto& row = rows[static_cast<std::size_t>(i)];
                const QString cells[5] = {QString::fromStdString(row.name), QString::fromStdString(row.status),
                                          QString::fromStdString(row.impact), QString::fromStdString(row.source),
                                          QString::fromStdString(row.command)};
                for (int c = 0; c < 5; ++c) table_->setItem(i, c, new QTableWidgetItem(cells[c]));
            }
            if (!r.error.isEmpty()) {
                error_->setText(r.error);
            }
            error_->setVisible(!error_->text().isEmpty());
            emit refreshed();
        });
}

void StartupPage::setEnabledAt(int row, bool enabled) {
    if (row < 0 || static_cast<std::size_t>(row) >= entries_.size()) return;
    const auto collector = collector_;
    const auto entry = entries_[static_cast<std::size_t>(row)];
    error_->clear();
    runAsync<QString>(
        this,
        [collector, entry, enabled] {
            try {
                collector->setEnabled(entry, enabled);
                return QString();
            } catch (const std::exception& e) {
                return QString::fromUtf8(e.what());
            }
        },
        [this](QString err) {
            error_->setText(err);
            refresh();
        });
}

}  // namespace pmp::gui
