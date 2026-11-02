// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/screensaver.hpp>

#include <QKeyEvent>
#include <QPainter>

#include <algorithm>

namespace pmp::gui {
namespace {

QColor withAlpha(vm::Rgb c, double alpha) {
    QColor q(c.r, c.g, c.b);
    q.setAlphaF(std::clamp(alpha, 0.0, 1.0));
    return q;
}

}  // namespace

Screensaver::Screensaver(QWidget* parent) : QWidget(parent, Qt::Window | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle("PerfMonPlus");
}

void Screensaver::setFrame(const Frame& f) { target_ = vm::channelLevels(f); }

void Screensaver::setTheme(const vm::Theme& theme) { theme_ = theme; }

void Screensaver::tick(double dt) {
    time_ += dt;
    visibleFor_ += dt;
    const double ease = std::min(1.0, dt * 3.0);  // levels glide, so a one-second sample never makes the tunnel jump
    shown_.cpu += (target_.cpu - shown_.cpu) * ease;
    shown_.memory += (target_.memory - shown_.memory) * ease;
    shown_.gpu += (target_.gpu - shown_.gpu) * ease;
    shown_.disk += (target_.disk - shown_.disk) * ease;
    if (isVisible()) update();
}

void Screensaver::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    visibleFor_ = 0;
    setFocus();
}

void Screensaver::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(theme_.background.r, theme_.background.g, theme_.background.b));
    p.setRenderHint(QPainter::Antialiasing);
    const vm::Rgb colours[4] = {theme_.cpu, theme_.memory, theme_.netRx, theme_.disk};
    const QPointF centre(width() / 2.0, height() / 2.0);
    for (const auto& ring : vm::tunnelRings(time_, shown_)) {
        const double halfW = width() / 2.0 * ring.scale;
        const double halfH = height() / 2.0 * ring.scale;
        p.setPen(QPen(withAlpha(colours[ring.channel], ring.alpha), 1.5 + 5.0 * ring.scale));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(centre.x() - halfW, centre.y() - halfH, halfW * 2, halfH * 2), 12 * ring.scale, 12 * ring.scale);
    }
    if (visibleFor_ < 4.0) {  // a hint that fades away
        p.setPen(withAlpha(theme_.textDim, 1.0 - visibleFor_ / 4.0));
        p.drawText(rect().adjusted(0, 0, 0, -24), Qt::AlignHCenter | Qt::AlignBottom, "Press any key to return");
    }
}

void Screensaver::dismiss() {
    hide();
    emit closed();
}

void Screensaver::keyPressEvent(QKeyEvent*) { dismiss(); }
void Screensaver::mousePressEvent(QMouseEvent*) { dismiss(); }

}  // namespace pmp::gui
