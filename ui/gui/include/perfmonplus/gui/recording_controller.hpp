// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/app_settings.hpp>
#include <perfmonplus/gui/sampling_service.hpp>
#include <perfmonplus/trace/recorder.hpp>

#include <QObject>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>

namespace pmp::gui {

// The Flight Recorder (§8.2) as the window sees it: a button that starts and stops a recording, an optional rolling
// "black box" buffer, and saving to a .pmptrace file. The recorded data lives in memory as compressed chunks.
class RecordingController : public QObject {
    Q_OBJECT
public:
    explicit RecordingController(QObject* parent = nullptr);

    void setHost(trace::HostMeta host) {
        host_ = std::move(host);
        if (rec_) rec_->setHost(host_);
    }
    // Applies the recording settings. In ring mode the recorder starts by itself; leaving ring mode drops the buffer.
    void configure(const AppSettings& settings, std::chrono::milliseconds interval);

    bool recording() const;
    bool ring() const { return settings_.recordingRing; }
    bool hasRecording() const;
    bool dirty() const { return dirty_ && !ring(); }  // a manual recording not yet saved
    std::size_t sampleCount() const;
    double worstAppendMs() const { return worstAppendMs_; }  // the slowest onFrame so far (a chunk seal included)
    QString timecode() const;
    QString sizeText() const;

    void toggle();
    void start();
    void stop();
    void discard();
    void onFrame(const FrameUpdate& update);
    // `lastMinutes` saves only the end of the recording (ring mode). Returns false and fills `error` on failure.
    bool save(const QString& path, QString* error, std::optional<int> lastMinutes = {});

    static QString formatTimecode(std::chrono::milliseconds elapsed);  // HH:MM:SS.ff
    static QString formatSize(std::size_t bytes);                      // "≈ 68 KB", "≈ 1.4 MB"

signals:
    void stateChanged();
    void progress(const QString& timecode, const QString& sizeText);
    void limitReached(int minutes);

private:
    void makeRecorder();

    AppSettings settings_;
    std::chrono::milliseconds interval_{1000};
    trace::HostMeta host_;
    std::unique_ptr<trace::Recorder> rec_;
    bool dirty_ = false;
    bool wasFull_ = false;
    double worstAppendMs_ = 0;
};

}  // namespace pmp::gui
