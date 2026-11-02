// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/users_page.hpp>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace pmp::gui {

UsersPage::UsersPage(std::shared_ptr<const linux::SessionsCollector> collector, QWidget* parent)
    : QWidget(parent), collector_(std::move(collector)), table_(new QTableWidget(0, 6, this)), error_(new QLabel(this)) {
    error_->setWordWrap(true);
    error_->hide();
    table_->setHorizontalHeaderLabels({"User", "Sessions", "Status", "CPU", "Memory", "Disk"});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 200);

    auto* lock = new QPushButton("Lock", this);
    auto* signOffBtn = new QPushButton("Sign off", this);
    auto* bar = new QHBoxLayout;
    bar->addWidget(lock);
    bar->addWidget(signOffBtn);
    bar->addStretch(1);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    col->addWidget(error_);
    col->addWidget(table_, 1);
    col->addLayout(bar);
    connect(lock, &QPushButton::clicked, this, [this] { lockSession(table_->currentRow()); });
    connect(signOffBtn, &QPushButton::clicked, this, [this] { signOff(table_->currentRow()); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && static_cast<std::size_t>(row) < rows_.size()) emit userProcessesRequested(QString::fromStdString(rows_[static_cast<std::size_t>(row)].user));
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (row >= 0 && table_->item(row, 0)) selectedUser_ = table_->item(row, 0)->text();
    });
    timer_.setInterval(5000);
    connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
}

void UsersPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refresh();
    timer_.start();
}

void UsersPage::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    timer_.stop();
}

void UsersPage::update(const Frame& frame, const HistoryStore&, MonoTime) {
    frame_ = frame;
    if (isVisible() || !sessions_.empty()) fill();
}

void UsersPage::fill() {
    rows_ = vm::userRows(sessions_, frame_);
    const QSignalBlocker block(table_);  // rebuilding must not overwrite what the user selected
    table_->setRowCount(static_cast<int>(rows_.size()));
    int selected = -1;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const auto& r = rows_[static_cast<std::size_t>(i)];
        const std::string cells[6] = {r.user, r.sessions, r.status, r.cpu, r.memory, r.diskPerSec};
        for (int c = 0; c < 6; ++c) table_->setItem(i, c, new QTableWidgetItem(QString::fromStdString(cells[c])));
        if (QString::fromStdString(r.user) == selectedUser_) selected = i;
    }
    table_->setCurrentCell(selected, selected >= 0 ? 0 : -1);
}

void UsersPage::showErrors() {
    QStringList parts;
    if (!listError_.isEmpty()) parts << listError_;
    if (!actionError_.isEmpty()) parts << actionError_;
    error_->setText(parts.join("\n"));
    error_->setVisible(!parts.isEmpty());
}

void UsersPage::refresh() {
    const auto collector = collector_;
    runAsync<Result>(
        this,
        [collector] {
            Result r;
            try {
                r.sessions = collector->collect();
            } catch (const std::exception& e) {
                r.error = QString::fromUtf8(e.what());
            }
            return r;
        },
        [this](Result r) {
            sessions_ = std::move(r.sessions);
            listError_ = r.error;
            showErrors();
            fill();
            emit refreshed();
        });
}

void UsersPage::act(int row, bool terminate) {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return;
    const auto user = QString::fromStdString(rows_[static_cast<std::size_t>(row)].user);
    const auto ids = rows_[static_cast<std::size_t>(row)].sessionIds;
    if (terminate && confirm_ &&
        QMessageBox::question(this, "Sign off", QString("Sign off %1? Unsaved work will be lost.").arg(user)) != QMessageBox::Yes) {
        return;
    }
    const auto collector = collector_;
    actionError_.clear();
    showErrors();
    runAsync<QString>(
        this,
        [collector, ids, terminate] {
            QString err;
            for (const auto& id : ids) {
                const auto r = terminate ? collector->terminate(id) : collector->lock(id);
                if (r.exitCode != 0) err = QString::fromStdString(linux::describeFailure(r));
            }
            return err;
        },
        [this, terminate](QString err) {
            actionError_ = err.isEmpty() ? QString() : QString("%1 failed: %2").arg(terminate ? "Sign off" : "Lock", err);
            showErrors();
            refresh();  // the list changes underneath, but the action's own error stays on screen
        });
}

void UsersPage::lockSession(int row) { act(row, false); }
void UsersPage::signOff(int row) { act(row, true); }

}  // namespace pmp::gui
