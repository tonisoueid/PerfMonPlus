// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/replay_controller.hpp>

#include <perfmonplus/gui/recording_controller.hpp>

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>

namespace pmp::gui {

ReplayController::ReplayController(QObject* parent) : QObject(parent) {
    timer_.setInterval(100);  // only runs while playing (NFR-04)
    connect(&timer_, &QTimer::timeout, this, &ReplayController::onTimer);
}

void ReplayController::begin(std::shared_ptr<trace::Player> player, const QString& path, const trace::OpenResult& result) {
    end();
    player_ = std::move(player);
    fileName_ = QFileInfo(path).fileName();
    note_ = QString::fromStdString(result.note);
    transport_ = vm::Transport(player_->sampleCount(), std::chrono::milliseconds(std::max(1, player_->info().intervalMs)));
    shown_.reset();
    steadyAnchor_ = std::chrono::steady_clock::now();
    systemAnchor_ = std::chrono::system_clock::now();
    showAt(0, false);
    emit stateChanged();
}

void ReplayController::end() {
    timer_.stop();
    if (!player_) return;
    player_.reset();
    frame_.reset();
    history_.reset();
    processHistory_.clear();
    shown_.reset();
    emit stateChanged();
}

MonoTime ReplayController::mapMono(std::chrono::system_clock::time_point wall) const {
    return steadyAnchor_ + std::chrono::duration_cast<std::chrono::nanoseconds>(wall - systemAnchor_);
}

MonoTime ReplayController::monoAtPosition() const { return mapMono(wallAtPosition()); }

std::shared_ptr<Frame> ReplayController::loadFrame(std::size_t index) const {
    auto f = player_->frame(index);
    if (!f) return nullptr;
    f->mono = mapMono(f->wall);
    return std::make_shared<Frame>(std::move(*f));
}

QString ReplayController::banner() const {
    if (!player_) return {};
    const auto& i = player_->info();
    const QString date = QLocale().toString(QDateTime::fromMSecsSinceEpoch(i.startWallMs), QLocale::ShortFormat);
    const QString os = i.host.os.empty() ? QStringLiteral("an unknown system") : QString::fromStdString(i.host.os);
    return QStringLiteral("Replaying %1 — recorded %2 on %3").arg(fileName_, date, os);
}

bool ReplayController::gapAtPosition() const { return player_ && player_->gapBefore(transport_.pos()); }

std::chrono::system_clock::time_point ReplayController::wallAtPosition() const {
    return player_ ? player_->wallAt(transport_.pos()) : std::chrono::system_clock::time_point{};
}

QString ReplayController::positionText() const {
    if (!player_) return {};
    const auto first = player_->wallAt(0);
    const auto at = std::chrono::duration_cast<std::chrono::milliseconds>(player_->wallAt(transport_.pos()) - first);
    const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(player_->wallAt(player_->sampleCount() - 1) - first);
    return RecordingController::formatTimecode(at) + " / " + RecordingController::formatTimecode(total);
}

QString ReplayController::clockText() const {
    if (!player_) return {};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(wallAtPosition().time_since_epoch()).count();
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QLocale::LongFormat);
}

void ReplayController::setHistoryWindow(int seconds) {
    windowSeconds_ = std::clamp(seconds, 1, 3600);
    if (player_ && shown_) showAt(*shown_, false);
}

void ReplayController::seek(std::size_t index) {
    if (!player_) return;
    transport_.seek(index);
    showAt(transport_.pos(), false);
}

void ReplayController::step(int samples) {
    if (!player_) return;
    const bool wasPlaying = transport_.playing();
    transport_.step(samples);
    if (wasPlaying) timer_.stop();
    showAt(transport_.pos(), samples == 1);
    emit stateChanged();
}

void ReplayController::jump(std::chrono::seconds s) {
    if (!player_) return;
    transport_.jump(s);
    showAt(transport_.pos(), false);
}

void ReplayController::toEnd() {
    if (!player_) return;
    transport_.toEnd();
    showAt(transport_.pos(), false);
}

void ReplayController::togglePlay() {
    if (!player_) return;
    const auto before = transport_.pos();
    transport_.toggle();
    if (transport_.playing()) {
        lastTickMs_ = QDateTime::currentMSecsSinceEpoch();
        timer_.start();
        if (transport_.pos() != before) showAt(transport_.pos(), false);  // playing from the end restarts
    } else {
        timer_.stop();
    }
    emit stateChanged();
}

void ReplayController::setSpeed(double speed) {
    transport_.setSpeed(speed);
    emit stateChanged();
}

void ReplayController::onTimer() {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const double dt = static_cast<double>(now - lastTickMs_) / 1000.0;
    lastTickMs_ = now;
    const auto before = transport_.pos();
    if (transport_.advance(dt)) {
        // One sample ahead is an incremental update; a faster step (high speed) rebuilds the window.
        showAt(transport_.pos(), transport_.pos() == before + 1);
    }
    if (!transport_.playing()) {
        timer_.stop();
        emit stateChanged();
    }
}

// Makes `index` the shown sample. `incremental` is true only when it directly follows the last one shown.
void ReplayController::showAt(std::size_t index, bool incremental) {
    if (!player_ || index >= player_->sampleCount()) return;
    const auto frame = loadFrame(index);
    if (!frame) return;  // an unreadable chunk: keep what is shown
    if (incremental && shown_ && *shown_ + 1 == index) {
        history_.ingest(*frame, player_->gapBefore(index));
        processHistory_.ingest(*frame, player_->gapBefore(index));
    } else {
        history_.reset();  // in place: the pages' graphs hold pointers to these rings
        processHistory_.clear();
        const auto interval = std::max(1, player_->info().intervalMs);
        const auto back = static_cast<std::size_t>(windowSeconds_) * 1000 / static_cast<std::size_t>(interval) + 1;
        const std::size_t from = index > back ? index - back : 0;
        for (std::size_t j = from; j < index; ++j) {
            if (const auto f = loadFrame(j)) {
                const bool gap = j != from && player_->gapBefore(j);
                history_.ingest(*f, gap);
                processHistory_.ingest(*f, gap);
            }
        }
        history_.ingest(*frame, index != from && player_->gapBefore(index));
        processHistory_.ingest(*frame, index != from && player_->gapBefore(index));
    }
    frame_ = frame;
    shown_ = index;
    emit positionChanged();
}

}  // namespace pmp::gui
