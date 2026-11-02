// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/status_bar.hpp>

#include <QHBoxLayout>

#include <algorithm>

namespace pmp::gui {

StatusBar::StatusBar(QWidget* parent) : QWidget(parent), dot_(new QLabel("●", this)), text_(new QLabel(this)) {
    setObjectName("statusBar");
    text_->setTextFormat(Qt::PlainText);  // the replay banner carries a file name
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->addWidget(dot_);
    layout->addWidget(text_);
    layout->addStretch(1);

    size_ = new QLabel(this);
    size_->setObjectName("recordSize");
    rec_ = new QToolButton(this);
    rec_->setObjectName("recordDot");
    rec_->setText("●");
    rec_->setAutoRaise(true);
    rec_->setCursor(Qt::PointingHandCursor);
    rec_->setToolTip("Record a trace (Ctrl+R)");
    rec_->setAccessibleName("Record");
    rec_->setStyleSheet("QToolButton { color: #7a2a2e; border: none; font-size: 16px; }");
    time_ = new QLabel("00:00:00.00", this);
    time_->setObjectName("timecode");
    time_->setEnabled(false);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    time_->setFont(mono);
    layout->addWidget(size_);
    layout->addWidget(rec_);
    layout->addWidget(time_);
    video_ = new QLabel("● VIDEO 00:00", this);
    video_->setObjectName("videoIndicator");
    video_->setStyleSheet("color: #ff3b42;");
    video_->setToolTip("Recording video uses extra CPU (screen capture and encoding) until you stop it.");
    video_->hide();
    layout->insertWidget(layout->indexOf(size_), video_);
    blink_.setInterval(500);
    connect(&blink_, &QTimer::timeout, this, [this] {
        blinkOn_ = !blinkOn_;
        video_->setStyleSheet(blinkOn_ ? "color: #ff3b42;" : "color: #6e2428;");
    });
    connect(rec_, &QToolButton::clicked, this, &StatusBar::recordToggled);
    pulse_.setInterval(600);
    connect(&pulse_, &QTimer::timeout, this, [this] {
        pulseOn_ = !pulseOn_;
        rec_->setStyleSheet(QString("QToolButton { color: %1; border: none; font-size: 16px; }").arg(pulseOn_ ? "#ff3b42" : "#8f1f25"));
    });
    dot_->setProperty("health", "ok");
    dot_->setStyleSheet("color:#43d65a;");
    update(Frame{});
}

void StatusBar::update(const Frame& frame) {
    if (!message_.isEmpty()) return;  // a message (replay) owns the line until it is cleared
    if (frame.generation == 0) {  // nothing sampled yet: say so rather than show made-up counts
        healthy_ = true;
        text_->setText(QStringLiteral("Starting…"));
        return;
    }
    const auto degraded = std::count_if(frame.providerHealth.begin(), frame.providerHealth.end(), [](const ProviderHealth& h) {
        return h.state != HealthState::Healthy && h.state != HealthState::Disabled;
    });
    const auto disabled = std::count_if(frame.providerHealth.begin(), frame.providerHealth.end(),
                                        [](const ProviderHealth& h) { return h.state == HealthState::Disabled; });
    healthy_ = degraded == 0;
    const QString procs = frame.processes.count.isMeasured()
                              ? QString::number(static_cast<long long>(*frame.processes.count.value))
                              : QStringLiteral("—");
    const QString lead = healthy_ ? QStringLiteral("Native providers healthy")
                                  : QStringLiteral("%1 provider(s) degraded").arg(degraded);
    const QString off = disabled > 0 ? QStringLiteral(" · %1 disabled").arg(disabled) : QString();
    text_->setText(QStringLiteral("%1%2 · %3 processes · Generation %4").arg(lead, off, procs).arg(frame.generation));
    // setStyleSheet() re-polishes the widget even for identical text, so only touch it on a change.
    if (healthy_ != dotHealthy_) {
        dot_->setStyleSheet(healthy_ ? "color:#43d65a;" : "color:#f5a524;");
        dotHealthy_ = healthy_;
    }
}

QString StatusBar::text() const { return text_->text(); }

void StatusBar::setRecording(bool on) {
    if (on == recording_) return;
    recording_ = on;
    rec_->setProperty("recording", on);
    time_->setEnabled(on);
    if (on) {
        pulseOn_ = true;
        rec_->setStyleSheet("QToolButton { color: #ff3b42; border: none; font-size: 16px; }");
        pulse_.start();
    } else {
        pulse_.stop();
        rec_->setStyleSheet("QToolButton { color: #7a2a2e; border: none; font-size: 16px; }");
    }
    rec_->setToolTip(on ? "Stop recording (Ctrl+R)" : "Record a trace (Ctrl+R)");
}

void StatusBar::setRingMode(bool on) {
    rec_->setToolTip(on ? "Save what just happened… (Ctrl+R)" : "Record a trace (Ctrl+R)");
    rec_->setAccessibleName(on ? "Save last minutes" : "Record");
}

void StatusBar::setVideoRecording(bool on) {
    video_->setVisible(on);
    if (on) {
        blinkOn_ = true;
        video_->setStyleSheet("color: #ff3b42;");
        blink_.start();
    } else {
        blink_.stop();
    }
}

void StatusBar::setVideoElapsed(const QString& text) { video_->setText("● VIDEO " + text); }

void StatusBar::setTimecode(const QString& text) { time_->setText(text); }
void StatusBar::setSizeText(const QString& text) { size_->setText(text); }

void StatusBar::setMessage(const QString& text) {
    message_ = text;
    if (!text.isEmpty()) text_->setText(text);
}

}  // namespace pmp::gui
