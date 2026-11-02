// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>

#include <QObject>

#include <optional>

namespace pmp::gui {

// One shared hover cursor per page, so the graphs that show the same stretch of time (CPU and memory on Summary,
// the graphs of one Performance panel) draw the same vertical line at the same absolute moment (HIS-02).
class GraphCursorHub : public QObject {
    Q_OBJECT
public:
    explicit GraphCursorHub(QObject* parent = nullptr) : QObject(parent) {}

    void setCursor(std::optional<MonoTime> t) {
        if (t == cursor_) return;
        cursor_ = t;
        emit cursorChanged();
    }
    std::optional<MonoTime> cursor() const { return cursor_; }

signals:
    void cursorChanged();

private:
    std::optional<MonoTime> cursor_;
};

}  // namespace pmp::gui
