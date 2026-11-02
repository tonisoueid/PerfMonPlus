// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/recording_controller.hpp>

namespace pmp::gui {

RecordingController::RecordingController(QObject* parent) : QObject(parent) { host_.os = "Linux"; }

void RecordingController::makeRecorder() {
    trace::RecorderOptions o;
    o.topProcesses = settings_.recordingTopProcesses;
    o.maxLength = std::chrono::minutes(settings_.recordingMaxMinutes);
    if (settings_.recordingRing) o.ring = std::chrono::minutes(settings_.recordingRingMinutes);
    rec_ = std::make_unique<trace::Recorder>(host_, interval_, o);
    wasFull_ = false;
}

void RecordingController::configure(const AppSettings& s, std::chrono::milliseconds interval) {
    const bool ringWas = settings_.recordingRing;
    settings_ = s;
    interval_ = interval;
    if (ringWas && !s.recordingRing && rec_) {  // leaving ring mode drops the rolling buffer
        discard();
        return;
    }
    if (rec_) {  // what was recorded stays: only the settings that can change on the fly are applied
        rec_->setInterval(interval);
        rec_->setMaxLength(std::chrono::minutes(s.recordingMaxMinutes));
        rec_->setTopProcesses(s.recordingTopProcesses);
        if (s.recordingRing) rec_->setRing(std::chrono::minutes(s.recordingRingMinutes));
    }
    if (s.recordingRing && !recording()) {
        if (!rec_) {
            makeRecorder();
            dirty_ = false;
        }
        rec_->start();
        emit stateChanged();
    }
}

bool RecordingController::recording() const { return rec_ && rec_->state() == trace::Recorder::State::Recording; }
bool RecordingController::hasRecording() const { return rec_ && rec_->sampleCount() > 0; }
std::size_t RecordingController::sampleCount() const { return rec_ ? rec_->sampleCount() : 0; }

QString RecordingController::timecode() const {
    return formatTimecode(rec_ ? rec_->elapsed() : std::chrono::milliseconds(0));
}

QString RecordingController::sizeText() const { return rec_ && rec_->sampleCount() ? formatSize(rec_->compressedBytes()) : QString(); }

void RecordingController::toggle() {
    if (recording()) stop();
    else start();
}

void RecordingController::start() {
    if (recording()) return;
    makeRecorder();  // a new manual recording replaces the previous (saved or not: the window asks before it gets here)
    dirty_ = false;
    rec_->start();
    emit stateChanged();
    emit progress(timecode(), sizeText());
}

void RecordingController::stop() {
    if (!rec_) return;
    rec_->stop();
    emit stateChanged();
    emit progress(timecode(), sizeText());
}

void RecordingController::discard() {
    rec_.reset();
    dirty_ = false;
    emit stateChanged();
    emit progress(timecode(), sizeText());
}

void RecordingController::onFrame(const FrameUpdate& update) {
    if (!rec_ || !update.frame || rec_->state() != trace::Recorder::State::Recording) return;
    const auto t0 = std::chrono::steady_clock::now();
    rec_->append(*update.frame, update.gapBefore);
    worstAppendMs_ = std::max(worstAppendMs_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    dirty_ = true;
    if (rec_->state() == trace::Recorder::State::Full && !wasFull_) {
        wasFull_ = true;
        emit stateChanged();
        emit limitReached(settings_.recordingMaxMinutes);
    }
    emit progress(timecode(), sizeText());
}

bool RecordingController::save(const QString& path, QString* error, std::optional<int> lastMinutes) {
    if (!rec_) {
        if (error) *error = "There is nothing recorded to save.";
        return false;
    }
    std::string err;
    std::optional<std::chrono::minutes> last;
    if (lastMinutes) last = std::chrono::minutes(*lastMinutes);
    const bool ok = rec_->save(path.toStdString(), &err, last);
    if (ok) {
        if (!ring()) dirty_ = false;
    } else if (error) {
        *error = QString::fromStdString(err);
    }
    return ok;
}

QString RecordingController::formatTimecode(std::chrono::milliseconds e) {
    const auto ms = std::max<long long>(0, e.count());
    const auto cs = ms % 1000 / 10;
    const auto s = ms / 1000 % 60, m = ms / 60000 % 60, h = ms / 3600000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

QString RecordingController::formatSize(std::size_t bytes) {
    if (bytes < 1'000'000) return QStringLiteral("≈ %1 KB").arg(std::max<std::size_t>(1, (bytes + 500) / 1000));
    return QStringLiteral("≈ %1 MB").arg(static_cast<double>(bytes) / 1e6, 0, 'f', 1);
}

}  // namespace pmp::gui
