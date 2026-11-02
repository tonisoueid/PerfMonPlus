// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/replay_bar.hpp>

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace pmp::gui {

ReplayBar::ReplayBar(ReplayController* replay, QWidget* parent) : QFrame(parent), replay_(replay) {
    setObjectName("replayBar");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 6, 14, 6);
    col->setSpacing(4);

    auto* top = new QHBoxLayout;
    banner_ = new QLabel(this);
    banner_->setObjectName("replayBanner");
    banner_->setTextFormat(Qt::PlainText);  // the file name comes from outside
    note_ = new QLabel(this);
    note_->setObjectName("dim");
    note_->setTextFormat(Qt::PlainText);
    return_ = new QPushButton("Return to Live", this);
    top->addWidget(banner_);
    top->addWidget(note_);
    top->addStretch(1);
    top->addWidget(return_);
    col->addLayout(top);

    auto* row = new QHBoxLayout;
    auto button = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        return b;
    };
    toStart_ = button("⏮", "Jump to start (Home)");
    back_ = button("◀", "Step back (Left)");
    play_ = button("▶", "Play (Space)");
    forward_ = button("▶|", "Step forward (Right)");
    toEnd_ = button("⏭", "Jump to end (End)");
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setAccessibleName("Playback position");
    position_ = new QLabel(this);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    position_->setFont(mono);
    clock_ = new QLabel(this);
    clock_->setObjectName("dim");
    speed_ = new QComboBox(this);
    speed_->setAccessibleName("Playback speed");
    for (const double s : vm::Transport::speeds()) speed_->addItem(QString("%1×").arg(s), s);
    for (auto* w : std::initializer_list<QWidget*>{toStart_, back_, play_, forward_, toEnd_}) row->addWidget(w);
    row->addWidget(slider_, 1);
    row->addWidget(position_);
    row->addWidget(clock_);
    row->addWidget(speed_);
    col->addLayout(row);

    connect(return_, &QPushButton::clicked, this, &ReplayBar::returnToLive);
    connect(toStart_, &QToolButton::clicked, replay_, &ReplayController::toStart);
    connect(back_, &QToolButton::clicked, replay_, [this] { replay_->step(-1); });
    connect(forward_, &QToolButton::clicked, replay_, [this] { replay_->step(1); });
    connect(toEnd_, &QToolButton::clicked, replay_, &ReplayController::toEnd);
    connect(play_, &QToolButton::clicked, replay_, &ReplayController::togglePlay);
    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        if (!syncing_ && v >= 0) replay_->seek(static_cast<std::size_t>(v));
    });
    connect(speed_, &QComboBox::activated, this, [this](int i) { replay_->setSpeed(speed_->itemData(i).toDouble()); });
    connect(replay_, &ReplayController::positionChanged, this, &ReplayBar::sync);
    connect(replay_, &ReplayController::stateChanged, this, &ReplayBar::sync);
    sync();
}

void ReplayBar::sync() {
    syncing_ = true;
    const bool on = replay_->active();
    banner_->setText(replay_->banner());
    note_->setText(replay_->note());
    note_->setVisible(!replay_->note().isEmpty());
    if (on) {
        const auto& t = replay_->transport();
        slider_->setRange(0, static_cast<int>(t.count()) - 1);
        slider_->setValue(static_cast<int>(t.pos()));
        play_->setText(t.playing() ? "⏸" : "▶");
        play_->setToolTip(t.playing() ? "Pause (Space)" : "Play (Space)");
        play_->setAccessibleName(t.playing() ? "Pause" : "Play");
        position_->setText(replay_->positionText());
        clock_->setText(replay_->clockText());
        speed_->setCurrentIndex(speed_->findData(t.speed()));
    }
    syncing_ = false;
}

}  // namespace pmp::gui
