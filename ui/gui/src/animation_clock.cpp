// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/animation_clock.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::gui {

AnimationClock::AnimationClock(QObject* parent) : QObject(parent) {
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(static_cast<int>(std::lround(1000.0 / hz_)));
    connect(&timer_, &QTimer::timeout, this, [this] {
        emit tick(static_cast<double>(elapsed_.restart()) / 1000.0);
    });
}

void AnimationClock::setFrameRate(int hz) {
    hz_ = std::clamp(hz, 1, 240);
    timer_.setInterval(std::max(1, static_cast<int>(std::lround(1000.0 / hz_))));
}

void AnimationClock::setActive(bool active) {
    if (active == timer_.isActive()) return;
    if (active) {
        elapsed_.restart();
        timer_.start();
    } else {
        timer_.stop();
    }
}

}  // namespace pmp::gui
