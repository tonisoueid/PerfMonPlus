// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/pages.hpp>

#include <QListWidget>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

namespace pmp::gui {

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);

    Page current() const;
    void setCurrent(Page p);
    QStringList labels() const;  // includes the "TOOLS" divider

signals:
    void pageSelected(pmp::gui::Page page);
    void settingsRequested();
    void colorsRequested();

private:
    QListWidget* list_;
};

}  // namespace pmp::gui
