// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/command.hpp>

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

class QWidget;

namespace pmp::gui {

// What it takes to record video (VID-02): `ffmpeg` with a VP9 or H.264 encoder. When something is missing the reason
// says what to install; nothing fails silently.
struct VideoSupport {
    bool ok = false;
    QString ffmpeg;      // the executable
    QString encoder;     // "libvpx-vp9" or "libx264"
    QString container;   // "webm" or "mp4"
    QString reason;      // when !ok
};

// `findFfmpeg` returns the path of ffmpeg on PATH ("" when there is none); `run` runs a command (argv, no shell).
VideoSupport probeVideoSupport(const std::function<QString()>& findFfmpeg, const linux::CommandRunner& run);
VideoSupport probeVideoSupport();  // the real PATH and the real ffmpeg

// Records the app's own window (never anything else on the screen) by sending its frames to ffmpeg. The picture size
// is fixed when recording starts: a window resized mid-recording is letterboxed into it.
class VideoRecorder : public QObject {
    Q_OBJECT
public:
    explicit VideoRecorder(QObject* parent = nullptr);
    ~VideoRecorder() override;

    bool start(QWidget* source, const QString& path, int fps, const VideoSupport& support, QString* error);
    void stop();  // finishes the file in the background; `finished` follows
    bool recording() const { return recording_; }
    bool timerActive() const { return timer_.isActive(); }
    qint64 elapsedMs() const;
    int framesWritten() const { return frames_; }
    int framesDropped() const { return dropped_; }
    QSize frameSize() const { return size_; }
    static QString safeOutputPath(const QString& path);  // absolute, never starting with a dash

signals:
    void finished(const QString& path, bool ok, const QString& message);

private:
    void captureFrame();
    void onProcessFinished(int code, QProcess::ExitStatus status);

    QWidget* source_ = nullptr;
    QProcess proc_;
    QTimer timer_, killTimer_;
    QSize size_;
    QString path_;
    bool recording_ = false;
    bool stopping_ = false;
    int frames_ = 0, dropped_ = 0;
    qint64 startedMs_ = 0, stoppedMs_ = 0;
};

}  // namespace pmp::gui
