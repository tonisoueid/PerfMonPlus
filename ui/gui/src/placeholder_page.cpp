// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/placeholder_page.hpp>

#include <QLabel>
#include <QVBoxLayout>

namespace pmp::gui {

PlaceholderPage::PlaceholderPage(const QString& name, QWidget* parent) : QWidget(parent) {
    auto* label = new QLabel(QStringLiteral("%1 arrives in a later phase.").arg(name), this);
    label->setAlignment(Qt::AlignCenter);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label);
}

}  // namespace pmp::gui
