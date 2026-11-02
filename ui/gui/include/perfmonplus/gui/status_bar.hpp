// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <QLabel>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace pmp::gui {

// SHL-10/11: provider health text on the left; Flight Recorder button, timecode and size estimate on the right.
class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

    void update(const Frame& frame);
    QString text() const;
    bool healthy() const { return healthy_; }

    // Flight Recorder (FLR-01): a red button that pulses while recording, a counting timecode and a size estimate.
    void setRecording(bool on);
    void setRingMode(bool on);  // the button then means "save what just happened"
    void setTimecode(const QString& text);
    void setSizeText(const QString& text);
    Qt::TextFormat messageFormat() const { return text_->textFormat(); }
    QString timecodeText() const { return time_->text(); }
    QString sizeText() const { return size_->text(); }
    bool recording() const { return recording_; }
    bool pulseActive() const { return pulse_.isActive(); }  // never running while idle (NFR-04)
    QToolButton* recordButton() const { return rec_; }
    // Screen video recording (VID-01): a blinking indicator and the elapsed time, only while recording.
    void setVideoRecording(bool on);
    void setVideoElapsed(const QString& text);
    QString videoText() const { return video_->text(); }
    QString videoToolTip() const { return video_->toolTip(); }
    bool videoVisible() const { return !video_->isHidden(); }
    bool videoBlinkActive() const { return blink_.isActive(); }
    void setMessage(const QString& text);  // a short line in place of the health text, e.g. "Replaying …"

signals:
    void recordToggled();

private:
    QLabel* dot_;
    QLabel* text_;
    QToolButton* rec_;
    QLabel* time_;
    QLabel* size_;
    QTimer pulse_, blink_;
    QLabel* video_;
    bool blinkOn_ = true;
    bool recording_ = false;
    bool pulseOn_ = false;
    QString message_;
    bool healthy_ = true;
    bool dotHealthy_ = true;  // what the dot's stylesheet currently shows
};

}  // namespace pmp::gui
