// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/trace/player.hpp>
#include <perfmonplus/vm/transport.hpp>

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <memory>

namespace pmp::gui {

// Replay (§8.2 FLR-05/06): plays a trace through the same pages as live data. It owns the history rings the pages
// read while replaying, so the live history is never touched. Seeking rebuilds them from the recorded window
// before the playhead; playing adds one sample at a time. Gaps stay gaps.
class ReplayController : public QObject {
    Q_OBJECT
public:
    explicit ReplayController(QObject* parent = nullptr);

    void begin(std::shared_ptr<trace::Player> player, const QString& path, const trace::OpenResult& result);
    void end();
    bool active() const { return player_ != nullptr; }

    QString fileName() const { return fileName_; }
    QString note() const { return note_; }     // "Recovered 89 seconds; …" or empty
    QString banner() const;                    // "Replaying {file} — recorded {date} on {os}"
    const trace::TraceInfo& info() const { return player_->info(); }
    vm::Transport& transport() { return transport_; }
    const vm::Transport& transport() const { return transport_; }
    std::shared_ptr<const Frame> frame() const { return frame_; }
    bool gapAtPosition() const;
    const HistoryStore& history() const { return history_; }
    const ProcessHistory& processHistory() const { return processHistory_; }
    std::chrono::system_clock::time_point wallAtPosition() const;
    // Recorded instants are placed on the steady clock at wall(i) - system_now + steady_now, so converting a graph
    // time back to a wall clock (hover tooltips, culprit lists) gives the time it was recorded.
    MonoTime monoAtPosition() const;
    QString positionText() const;              // "00:00:07.00 / 00:00:19.00" of recorded time, gaps included
    QString clockText() const;                 // the time of day the playhead is at

    void setHistoryWindow(int seconds);
    void seek(std::size_t index);
    void step(int samples);
    void jump(std::chrono::seconds s);
    void toStart() { seek(0); }
    void toEnd();
    void togglePlay();
    void setSpeed(double speed);

signals:
    void positionChanged();   // the frame or history changed
    void stateChanged();      // started, ended, play/pause, speed

private:
    void showAt(std::size_t index, bool incremental);
    MonoTime mapMono(std::chrono::system_clock::time_point wall) const;
    std::shared_ptr<Frame> loadFrame(std::size_t index) const;  // the recorded frame with its time on the steady clock
    void onTimer();

    std::shared_ptr<trace::Player> player_;
    QString fileName_, note_;
    vm::Transport transport_{0, std::chrono::milliseconds(1000)};
    std::shared_ptr<const Frame> frame_;
    HistoryStore history_{3600};
    ProcessHistory processHistory_;
    int windowSeconds_ = 60;
    std::optional<std::size_t> shown_;
    QTimer timer_;
    MonoTime steadyAnchor_{};
    std::chrono::system_clock::time_point systemAnchor_{};
    qint64 lastTickMs_ = 0;
};

}  // namespace pmp::gui
