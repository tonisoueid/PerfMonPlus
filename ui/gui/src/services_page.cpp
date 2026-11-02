// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/services_page.hpp>

#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace pmp::gui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

}  // namespace

ServicesPage::ServicesPage(std::shared_ptr<const linux::ServicesCollector> collector, QWidget* parent)
    : QWidget(parent),
      collector_(std::move(collector)),
      filter_(new QLineEdit(this)),
      status_(new QComboBox(this)),
      scope_(new QComboBox(this)),
      error_(new QLabel(this)),
      table_(new QTableWidget(0, 6, this)) {
    filter_->setPlaceholderText("Filter by unit or description");
    status_->addItems({"All", "Running", "Stopped", "Failed"});
    scope_->addItems({"System", "User"});
    error_->setWordWrap(true);
    error_->hide();

    table_->setHorizontalHeaderLabels({"Unit", "Description", "Status", "Start type", "PID", "Scope"});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 260);
    table_->setColumnWidth(1, 300);

    auto* refreshBtn = new QPushButton("Refresh", this);
    auto* startBtn = new QPushButton("Start", this);
    auto* stopBtn = new QPushButton("Stop", this);
    auto* restartBtn = new QPushButton("Restart", this);
    auto* logsBtn = new QPushButton("Logs…", this);

    auto* bar = new QHBoxLayout;
    bar->addWidget(filter_, 1);
    bar->addWidget(status_);
    bar->addWidget(scope_);
    bar->addWidget(refreshBtn);
    auto* actions = new QHBoxLayout;
    actions->addWidget(startBtn);
    actions->addWidget(stopBtn);
    actions->addWidget(restartBtn);
    actions->addWidget(logsBtn);
    actions->addStretch(1);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    col->addLayout(bar);
    col->addWidget(error_);
    col->addWidget(table_, 1);
    col->addLayout(actions);

    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (row >= 0 && table_->item(row, 0)) selectedUnit_ = table_->item(row, 0)->text();  // the unit, not the row
    });
    connect(filter_, &QLineEdit::textChanged, this, [this] { fill(); });
    connect(status_, &QComboBox::currentIndexChanged, this, [this] { fill(); });
    connect(scope_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(startBtn, &QPushButton::clicked, this, [this] { runAction("start", table_->currentRow()); });
    connect(stopBtn, &QPushButton::clicked, this, [this] { runAction("stop", table_->currentRow()); });
    connect(restartBtn, &QPushButton::clicked, this, [this] { runAction("restart", table_->currentRow()); });
    connect(logsBtn, &QPushButton::clicked, this, [this] { showLogs(); });
    timer_.setInterval(5000);
    connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
}

linux::ServiceScope ServicesPage::scope() const {
    return scope_->currentIndex() == 1 ? linux::ServiceScope::User : linux::ServiceScope::System;
}

void ServicesPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refresh();
    timer_.start();
}

void ServicesPage::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    timer_.stop();
}

void ServicesPage::refresh() {
    if (refreshing_) {
        refreshAgain_ = true;
        return;
    }
    refreshing_ = true;
    const auto collector = collector_;
    const auto sc = scope();
    runAsync<Result>(
        this,
        [collector, sc] {
            Result r;
            try {
                r.services = collector->collect(sc);
            } catch (const std::exception& e) {
                r.error = QString::fromUtf8(e.what());
            }
            return r;
        },
        [this](Result r) {
            refreshing_ = false;
            listError_ = r.error;
            if (r.error.isEmpty()) services_ = std::move(r.services);
            else services_.clear();
            fill();
            emit refreshed();
            if (refreshAgain_) {
                refreshAgain_ = false;
                refresh();
            }
        });
}

void ServicesPage::fill() {
    static const vm::StatusFilter filters[] = {vm::StatusFilter::All, vm::StatusFilter::Running, vm::StatusFilter::Stopped,
                                               vm::StatusFilter::Failed};
    const auto rows = vm::serviceRows(services_, filter_->text().toStdString(), filters[std::max(0, status_->currentIndex())]);
    {
        // Rows move when units appear or vanish, so the selection is restored by unit name. The rebuild itself
        // must not overwrite what was selected.
        const QSignalBlocker block(table_);
        table_->setRowCount(static_cast<int>(rows.size()));
        int selected = -1;
        for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
            const auto& row = rows[static_cast<std::size_t>(r)];
            const QString cells[6] = {q(row.unit), q(row.description), q(row.status), q(row.startType), q(row.pid), q(row.scope)};
            for (int c = 0; c < 6; ++c) table_->setItem(r, c, new QTableWidgetItem(cells[c]));
            if (cells[0] == selectedUnit_) selected = r;
        }
        if (selected >= 0) table_->setCurrentCell(selected, 0);
        else table_->setCurrentCell(-1, -1);
    }
    const QString err = lastError();
    error_->setText(err);
    error_->setVisible(!err.isEmpty());
}

void ServicesPage::runAction(const QString& action, int row) {
    if (row < 0 || row >= table_->rowCount()) return;
    const QString unit = table_->item(row, 0)->text();
    if (confirm_ && scope() == linux::ServiceScope::System && (action == "stop" || action == "restart") &&
        QMessageBox::question(this, "Confirm", QString("%1 %2? This may affect the system.").arg(action, unit)) != QMessageBox::Yes) {
        return;
    }
    actionError_.clear();
    const auto collector = collector_;
    const auto sc = scope();
    const std::string act = action.toStdString(), u = unit.toStdString();
    runAsync<linux::CommandResult>(
        this,
        [collector, sc, act, u] {
            try {
                return collector->act(sc, act, u);
            } catch (const std::exception& e) {
                return linux::CommandResult{1, "", e.what(), false};
            }
        },
        [this, action, unit](linux::CommandResult r) {
            if (r.exitCode != 0) {
                actionError_ = QString("%1 %2 failed: %3").arg(action, unit, q(linux::describeFailure(r))).trimmed();
            }
            refresh();  // always show the real state afterwards
        });
}

void ServicesPage::showLogs() {
    const int row = table_->currentRow();
    if (row < 0 || row >= table_->rowCount()) return;
    const std::string unit = table_->item(row, 0)->text().toStdString();
    const auto collector = collector_;
    const auto sc = scope();
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QString("Logs — %1").arg(q(unit)));
    dialog->resize(900, 500);
    auto* text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    text->setPlainText("Loading…");
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(text);
    runAsync<QString>(
        dialog, [collector, sc, unit] { return QString::fromStdString(collector->logs(sc, unit)); },
        [text](QString s) { text->setPlainText(s.isEmpty() ? QString("No log entries.") : s); });
    dialog->show();
}

}  // namespace pmp::gui
