// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/culprits_popover.hpp>

#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>

#include <time.h>

#include <chrono>
#include <ctime>

namespace pmp::gui {
namespace {

QString timeOfDay(MonoTime t) {
    // The history stores steady-clock times; the clocks' current offset turns one into a wall-clock time.
    const auto wall = std::chrono::system_clock::now() - (std::chrono::steady_clock::now() - t);
    const std::time_t tt = std::chrono::system_clock::to_time_t(wall);
    tm parts{};
    localtime_r(&tt, &parts);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &parts);
    return QString::fromLatin1(buf);
}

}  // namespace

CulpritsPopover::CulpritsPopover(QWidget* parent)
    : QFrame(parent, Qt::Popup), title_(new QLabel(this)), message_(new QLabel(this)), table_(new QTableWidget(0, 4, this)) {
    setObjectName("card");
    title_->setTextFormat(Qt::PlainText);
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    table_->setHorizontalHeaderLabels({"Process", "Used", "", ""});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setColumnWidth(0, 200);
    table_->setColumnWidth(1, 140);
    table_->setColumnWidth(2, 110);
    table_->setColumnWidth(3, 130);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 10, 12, 10);
    col->addWidget(title_);
    col->addWidget(message_);
    col->addWidget(table_);
    setMinimumWidth(620);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row) { goTo(row); });
}

void CulpritsPopover::present(const std::vector<vm::CulpritRow>& rows, const QString& title, QPoint at) {
    title_->setText(title);
    message_->hide();
    table_->show();
    keys_.clear();
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        keys_.push_back(r.key);
        table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(r.name)));
        table_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(r.primary)));
        table_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(r.secondary) + (r.exited ? "  · exited" : "")));
        auto* go = new QPushButton("Go to process", table_);
        connect(go, &QPushButton::clicked, this, [this, i] { goTo(i); });
        table_->setCellWidget(i, 3, go);
    }
    table_->setFixedHeight(std::min(300, 28 + static_cast<int>(rows.size()) * 30));
    adjustSize();
    move(at);
    QFrame::show();
}

void CulpritsPopover::presentMessage(const QString& message, const QString& title, QPoint at) {
    title_->setText(title);
    message_->setText(message);
    message_->show();
    table_->hide();
    keys_.clear();
    adjustSize();
    move(at);
    QFrame::show();
}

void CulpritsPopover::goTo(int row) {
    if (row < 0 || row >= static_cast<int>(keys_.size())) return;
    hide();
    emit processRequested(keys_[static_cast<std::size_t>(row)]);
}

CulpritsPresenter::CulpritsPresenter(QWidget* parent) : QObject(parent), popover_(new CulpritsPopover(parent)) {
    connect(popover_, &CulpritsPopover::processRequested, this, &CulpritsPresenter::processRequested);
}

void CulpritsPresenter::attach(HistoryGraph* graph, ProcessHistory::Resource resource) {
    graph->setSelectable(true);
    connect(graph, &HistoryGraph::rangeSelected, this,
            [this, resource](MonoTime from, MonoTime to, QPoint at) { show(resource, from, to, at); });
}

void CulpritsPresenter::show(ProcessHistory::Resource resource, MonoTime from, MonoTime to, QPoint at) {
    const QString what = resource == ProcessHistory::Resource::Cpu ? "CPU" : "memory";
    QString title = QString("Highest %1 use, %2 – %3").arg(what, timeOfDay(from), timeOfDay(to));
    if (history_) {
        // The per-process history is bounded (memory budget), so say when a selection reaches back further than it.
        if (const auto start = history_->coverageStart(); start && from < *start) {
            title += QString(" (history only reaches back to %1)").arg(timeOfDay(*start));
        }
    }
    if (!history_) {
        popover_->presentMessage("Per-process history is off. Turn it on in Settings > Sampling to see which processes caused this.", title, at);
        return;
    }
    const auto culprits = history_->culprits(from, to, resource, static_cast<std::size_t>(std::max(1, cpus_)), 10);
    if (culprits.empty()) {
        popover_->presentMessage("No process data was recorded for this time range.", title, at);
        return;
    }
    popover_->present(vm::culpritRows(culprits, resource), title, at);
}

}  // namespace pmp::gui
