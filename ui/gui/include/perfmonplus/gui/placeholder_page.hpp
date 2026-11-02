// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <QString>
#include <QWidget>

namespace pmp::gui {

// Shown for destinations that arrive in a later phase.
class PlaceholderPage : public QWidget {
public:
    explicit PlaceholderPage(const QString& name, QWidget* parent = nullptr);
};

}  // namespace pmp::gui
