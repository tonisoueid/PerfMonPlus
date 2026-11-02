// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace pmp::gui {

// One timer drives every animation (ARC-04). It only runs while active, so a hidden
// or minimised window costs nothing (NFR-04).
class AnimationClock : public QObject {
    Q_OBJECT
public:
    explicit AnimationClock(QObject* parent = nullptr);

    void setFrameRate(int hz);  // clamped to 1..240
    int frameRate() const { return hz_; }
    int intervalMs() const { return timer_.interval(); }
    void setActive(bool active);
    bool isActive() const { return timer_.isActive(); }

signals:
    void tick(double dtSeconds);

private:
    QTimer timer_;
    QElapsedTimer elapsed_;
    int hz_ = 60;
};

}  // namespace pmp::gui
