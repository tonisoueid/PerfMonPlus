// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/replay_controller.hpp>

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>

namespace pmp::gui {

// The Replay banner and transport (FLR-05, FLR-06): "Replaying … — Return to Live", play/pause, step, scrub bar,
// speed. All state lives in the ReplayController; this only shows it and forwards clicks.
class ReplayBar : public QFrame {
    Q_OBJECT
public:
    ReplayBar(ReplayController* replay, QWidget* parent = nullptr);

    QString bannerText() const { return banner_->text(); }
    QPushButton* returnButton() const { return return_; }
    QToolButton* playButton() const { return play_; }
    QSlider* slider() const { return slider_; }
    QComboBox* speedBox() const { return speed_; }
    QString positionText() const { return position_->text(); }
    QString noteText() const { return note_->text(); }

signals:
    void returnToLive();

private:
    void sync();

    ReplayController* replay_;
    QLabel* banner_;
    QLabel* note_;
    QPushButton* return_;
    QToolButton *toStart_, *back_, *play_, *forward_, *toEnd_;
    QSlider* slider_;
    QLabel* position_;
    QLabel* clock_;
    QComboBox* speed_;
    bool syncing_ = false;
};

}  // namespace pmp::gui
