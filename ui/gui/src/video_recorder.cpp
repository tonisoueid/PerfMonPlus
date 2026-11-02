// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/video_recorder.hpp>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QWidget>

#include <algorithm>

namespace pmp::gui {
namespace {

const char* kNeeds = "Video recording needs ffmpeg with the VP9 or H.264 encoder. Install the 'ffmpeg' package, then try again.";

}  // namespace

VideoSupport probeVideoSupport(const std::function<QString()>& findFfmpeg, const linux::CommandRunner& run) {
    VideoSupport s;
    s.ffmpeg = findFfmpeg ? findFfmpeg() : QString();
    if (s.ffmpeg.isEmpty()) {
        s.reason = kNeeds;
        return s;
    }
    const auto r = run(std::vector<std::string>{s.ffmpeg.toStdString(), "-hide_banner", "-encoders"});
    if (r.exitCode != 0) {
        s.reason = kNeeds;
        return s;
    }
    const auto has = [&](const char* name) { return r.out.find(std::string(" ") + name + " ") != std::string::npos; };
    if (has("libvpx-vp9")) {
        s.encoder = "libvpx-vp9";
        s.container = "webm";
    } else if (has("libx264")) {
        s.encoder = "libx264";
        s.container = "mp4";
    } else {
        s.reason = kNeeds;
        return s;
    }
    s.ok = true;
    return s;
}

VideoSupport probeVideoSupport() {
    return probeVideoSupport([] { return QStandardPaths::findExecutable("ffmpeg"); },
                             [](const std::vector<std::string>& argv) { return linux::runCommand(argv, std::chrono::seconds(10)); });
}

QString VideoRecorder::safeOutputPath(const QString& path) {
    QString abs = QFileInfo(path).absoluteFilePath();
    if (abs.startsWith('-')) abs = QDir::current().absoluteFilePath(abs);  // cannot happen for an absolute path, kept as a guard
    return abs;
}

VideoRecorder::VideoRecorder(QObject* parent) : QObject(parent) {
    connect(&timer_, &QTimer::timeout, this, &VideoRecorder::captureFrame);
    killTimer_.setSingleShot(true);
    connect(&killTimer_, &QTimer::timeout, this, [this] { proc_.kill(); });
    connect(&proc_, &QProcess::finished, this, &VideoRecorder::onProcessFinished);
    connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !recording_) return;
        recording_ = false;
        timer_.stop();
        emit finished(path_, false, "Could not start ffmpeg.");
    });
}

VideoRecorder::~VideoRecorder() {
    timer_.stop();
    if (proc_.state() != QProcess::NotRunning) {
        proc_.closeWriteChannel();
        if (!proc_.waitForFinished(3000)) proc_.kill();
    }
}

qint64 VideoRecorder::elapsedMs() const {
    if (!startedMs_) return 0;
    return (recording_ ? QDateTime::currentMSecsSinceEpoch() : stoppedMs_) - startedMs_;
}

bool VideoRecorder::start(QWidget* source, const QString& path, int fps, const VideoSupport& support, QString* error) {
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (recording_ || stopping_) return fail("A video is already being recorded.");
    if (!support.ok) return fail(support.reason);
    if (!source) return fail("There is no window to record.");
    fps = std::clamp(fps, 5, 60);
    size_ = QSize(source->width() & ~1, source->height() & ~1);  // the encoders want even dimensions
    if (size_.width() < 16 || size_.height() < 16) return fail("The window is too small to record.");
    path_ = safeOutputPath(path);
    source_ = source;
    frames_ = dropped_ = 0;

    QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "bgra", "-s",
                        QString("%1x%2").arg(size_.width()).arg(size_.height()), "-r", QString::number(fps), "-i", "-", "-an"};
    if (support.encoder == "libvpx-vp9") args << "-c:v" << "libvpx-vp9" << "-b:v" << "0" << "-crf" << "34" << "-deadline" << "realtime" << "-cpu-used" << "5" << "-row-mt" << "1";
    else args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "24" << "-pix_fmt" << "yuv420p";
    args << path_;  // an absolute path: it cannot be read as an option
    proc_.setProgram(support.ffmpeg);
    proc_.setArguments(args);
    proc_.setProcessChannelMode(QProcess::SeparateChannels);
    proc_.start();
    if (!proc_.waitForStarted(3000)) return fail("Could not start ffmpeg.");
    recording_ = true;
    stopping_ = false;
    startedMs_ = QDateTime::currentMSecsSinceEpoch();
    stoppedMs_ = 0;
    timer_.start(1000 / fps);
    return true;
}

void VideoRecorder::captureFrame() {
    if (!recording_ || !source_) return;
    if (proc_.bytesToWrite() > static_cast<qint64>(size_.width()) * size_.height() * 4 * 8) {  // the encoder is behind: drop, do not queue
        ++dropped_;
        return;
    }
    QImage img = source_->grab().toImage().convertToFormat(QImage::Format_RGB32);  // memory order B,G,R,X = ffmpeg's "bgra"
    if (img.size() != size_) {  // the window was resized: letterbox it into the fixed picture
        QImage canvas(size_, QImage::Format_RGB32);
        canvas.fill(Qt::black);
        const QImage scaled = img.scaled(size_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter p(&canvas);
        p.drawImage((size_.width() - scaled.width()) / 2, (size_.height() - scaled.height()) / 2, scaled);
        img = canvas;
    }
    proc_.write(reinterpret_cast<const char*>(img.constBits()), static_cast<qint64>(img.sizeInBytes()));
    ++frames_;
}

void VideoRecorder::stop() {
    if (!recording_) return;
    recording_ = false;
    stopping_ = true;
    stoppedMs_ = QDateTime::currentMSecsSinceEpoch();
    timer_.stop();
    proc_.closeWriteChannel();  // ffmpeg finishes the file once its input ends
    killTimer_.start(20000);    // a stuck encoder is killed; the partial file stays
}

void VideoRecorder::onProcessFinished(int code, QProcess::ExitStatus status) {
    killTimer_.stop();
    const bool wasRecording = recording_;
    recording_ = false;
    stopping_ = false;
    timer_.stop();
    if (!stoppedMs_) stoppedMs_ = QDateTime::currentMSecsSinceEpoch();
    const QString err = QString::fromUtf8(proc_.readAllStandardError()).trimmed();
    if (status == QProcess::NormalExit && code == 0 && !wasRecording) {
        emit finished(path_, true, QString("Saved %1").arg(QFileInfo(path_).fileName()));
    } else {
        const QString why = wasRecording ? "The encoder stopped early" : "ffmpeg reported a problem";
        emit finished(path_, false, why + (err.isEmpty() ? QString() : ": " + err) + ". What was recorded so far is kept.");
    }
}

}  // namespace pmp::gui
