// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/sidebar.hpp>

#include <QLabel>
#include <QVBoxLayout>

namespace pmp::gui {

Sidebar::Sidebar(QWidget* parent) : QWidget(parent), list_(new QListWidget(this)) {
    setObjectName("sidebar");
    setFixedWidth(210);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setFrameShape(QFrame::NoFrame);

    for (int i = 0; i < kPageCount; ++i) {
        if (i == kFirstToolPage) {
            auto* divider = new QListWidgetItem("TOOLS", list_);
            divider->setFlags(Qt::NoItemFlags);
            divider->setData(Qt::UserRole, -1);
        }
        auto* item = new QListWidgetItem(pageLabel(static_cast<Page>(i)), list_);
        item->setData(Qt::UserRole, i);
    }
    list_->setCurrentRow(0);

    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item, QListWidgetItem*) {
        if (!item) return;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0) emit pageSelected(static_cast<Page>(idx));
    });

    auto* logo = new QLabel("PerfMonPlus", this);
    logo->setObjectName("logo");
    logo->setAlignment(Qt::AlignCenter);

    auto* settings = new QToolButton(this);
    settings->setText("⚙ Settings");
    settings->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* colors = new QToolButton(this);
    colors->setText("◐ Colors");
    connect(settings, &QToolButton::clicked, this, &Sidebar::settingsRequested);
    connect(colors, &QToolButton::clicked, this, &Sidebar::colorsRequested);

    auto* footer = new QHBoxLayout;
    footer->addWidget(settings);
    footer->addWidget(colors);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(list_, 1);
    layout->addWidget(logo);
    layout->addLayout(footer);
}

Page Sidebar::current() const {
    const auto* item = list_->currentItem();
    return item ? static_cast<Page>(item->data(Qt::UserRole).toInt()) : Page::Summary;
}

void Sidebar::setCurrent(Page p) {
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->data(Qt::UserRole).toInt() == static_cast<int>(p)) {
            const QSignalBlocker block(list_);
            list_->setCurrentRow(row);
            return;
        }
    }
}

QStringList Sidebar::labels() const {
    QStringList out;
    for (int row = 0; row < list_->count(); ++row) out << list_->item(row)->text();
    return out;
}

}  // namespace pmp::gui
