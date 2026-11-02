// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/vm/theme.hpp>
#include <perfmonplus/vm/tunnel.hpp>

#include <QWidget>

namespace pmp::gui {

// Full-screen "tunnel" of rings, one colour per channel (cpu, memory, gpu, disk), driven only by the latest
// frame it is given. Any key (or a click) closes it.
class Screensaver : public QWidget {
    Q_OBJECT
public:
    explicit Screensaver(QWidget* parent = nullptr);

    void setFrame(const Frame& f);
    void setTheme(const vm::Theme& theme);
    void tick(double dtSeconds);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    void dismiss();

    vm::Theme theme_ = vm::darkTheme();
    vm::ChannelLevels target_, shown_;
    double time_ = 0;
    double visibleFor_ = 0;
};

}  // namespace pmp::gui
