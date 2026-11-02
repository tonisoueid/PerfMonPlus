// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/video_recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

using namespace pmp;
using namespace pmp::gui;

namespace {

template <class Cond>
bool waitFor(Cond cond, int ms = 5000) {
    QEventLoop loop;
    QTimer poll, limit;
    limit.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cond()) loop.quit();
    });
    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10);
    limit.start(ms);
    loop.exec();
    return cond();
}

VideoSupport fake() {
    VideoSupport s;
    s.ok = true;
    s.ffmpeg = PMP_FAKE_FFMPEG;
    s.encoder = "libvpx-vp9";
    s.container = "webm";
    return s;
}

linux::CommandRunner encoders(const std::string& listing, int exit = 0) {
    return [listing, exit](const std::vector<std::string>&) {
        linux::CommandResult r;
        r.exitCode = exit;
        r.out = listing;
        return r;
    };
}

struct Seen {
    QStringList args;
    qint64 bytes = -1;
};

Seen read(const QString& path) {
    Seen s;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return s;
    for (const auto& line : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith("BYTES ")) s.bytes = line.mid(6).toLongLong();
        else s.args << line;
    }
    return s;
}

}  // namespace

TEST_CASE("probe prefers vp9, then h264, then says what is missing", "[gui][video]") {
    const std::string both = " V....D libx264              H.264\n V....D libvpx-vp9           VP9\n";
    const std::string h264 = " V....D libx264              H.264\n V....D libx264rgb           H.264 RGB\n";
    const std::string none = " V....D mpeg4                MPEG-4\n V....D libx264rgb           only the rgb variant\n";
    auto s = probeVideoSupport([] { return QString("/usr/bin/ffmpeg"); }, encoders(both));
    REQUIRE(s.ok);
    REQUIRE(s.encoder == "libvpx-vp9");
    REQUIRE(s.container == "webm");
    s = probeVideoSupport([] { return QString("/usr/bin/ffmpeg"); }, encoders(h264));
    REQUIRE(s.ok);
    REQUIRE(s.encoder == "libx264");
    REQUIRE(s.container == "mp4");
    s = probeVideoSupport([] { return QString("/usr/bin/ffmpeg"); }, encoders(none));
    REQUIRE_FALSE(s.ok);
    REQUIRE(s.reason == "Video recording needs ffmpeg with the VP9 or H.264 encoder. Install the 'ffmpeg' package, then try again.");
}

TEST_CASE("no ffmpeg, or one that cannot list its encoders, gives the install hint", "[gui][video]") {
    const QString hint = "Video recording needs ffmpeg with the VP9 or H.264 encoder. Install the 'ffmpeg' package, then try again.";
    auto s = probeVideoSupport([] { return QString(); }, encoders(""));
    REQUIRE_FALSE(s.ok);
    REQUIRE(s.reason == hint);
    s = probeVideoSupport([] { return QString("/usr/bin/ffmpeg"); }, encoders("", 1));
    REQUIRE_FALSE(s.ok);
    REQUIRE(s.reason == hint);
}

TEST_CASE("start refuses when video is unsupported and says why", "[gui][video]") {
    QWidget w;
    w.resize(320, 200);
    VideoRecorder rec;
    VideoSupport none;
    none.reason = "install ffmpeg";
    QString err;
    REQUIRE_FALSE(rec.start(&w, "/tmp/x.webm", 30, none, &err));
    REQUIRE(err == "install ffmpeg");
    REQUIRE_FALSE(rec.recording());
    REQUIRE_FALSE(rec.timerActive());  // NFR-04: nothing captured when not recording
}

TEST_CASE("start fixes an even frame size; every frame is exactly W*H*4 bytes", "[gui][video]") {
    QTemporaryDir dir;
    QWidget w;
    w.resize(321, 201);
    w.show();
    VideoRecorder rec;
    QString err;
    bool done = false, ok = false;
    QObject::connect(&rec, &VideoRecorder::finished, &rec, [&](const QString&, bool good, const QString&) {
        done = true;
        ok = good;
    });
    const QString out = dir.filePath("a.webm");
    REQUIRE(rec.start(&w, out, 30, fake(), &err));
    REQUIRE(rec.frameSize() == QSize(320, 200));
    waitFor([&] { return rec.framesWritten() >= 5; });
    w.resize(500, 300);  // resized mid-recording: letterboxed into the fixed picture
    const int before = rec.framesWritten();
    waitFor([&] { return rec.framesWritten() >= before + 5; });
    rec.stop();
    REQUIRE(waitFor([&] { return done; }));
    REQUIRE(ok);
    const auto seen = read(out);
    REQUIRE(seen.bytes == static_cast<qint64>(rec.framesWritten()) * 320 * 200 * 4);
    REQUIRE(seen.args.contains("-s"));
    REQUIRE(seen.args[seen.args.indexOf("-s") + 1] == "320x200");
    REQUIRE(seen.args[seen.args.indexOf("-r") + 1] == "30");
    REQUIRE(seen.args[seen.args.indexOf("-c:v") + 1] == "libvpx-vp9");
    REQUIRE(seen.args[seen.args.indexOf("-pix_fmt") + 1] == "bgra");
}

TEST_CASE("frames are captured at about the requested rate", "[gui][video]") {
    QTemporaryDir dir;
    QWidget w;
    w.resize(160, 120);
    w.show();
    VideoRecorder rec;
    QString err;
    REQUIRE(rec.start(&w, dir.filePath("r.webm"), 30, fake(), &err));
    waitFor([] { return false; }, 1000);
    rec.stop();
    waitFor([&] { return !rec.timerActive(); });
    INFO("frames " << rec.framesWritten() << " dropped " << rec.framesDropped());
    REQUIRE(rec.framesWritten() + rec.framesDropped() >= 20);
    REQUIRE(rec.framesWritten() + rec.framesDropped() <= 40);
    REQUIRE(rec.elapsedMs() >= 900);
}

TEST_CASE("an encoder that fails is reported with its message and the partial file is kept", "[gui][video]") {
    QTemporaryDir dir;
    qputenv("PMP_FAKE_FFMPEG_EXIT", "1");
    qputenv("PMP_FAKE_FFMPEG_ERR", "No space left on device");
    QWidget w;
    w.resize(160, 120);
    VideoRecorder rec;
    QString err, message;
    bool done = false, ok = true;
    QObject::connect(&rec, &VideoRecorder::finished, &rec, [&](const QString&, bool good, const QString& m) {
        done = true;
        ok = good;
        message = m;
    });
    const QString out = dir.filePath("f.webm");
    REQUIRE(rec.start(&w, out, 30, fake(), &err));
    waitFor([&] { return rec.framesWritten() >= 3; });
    rec.stop();
    REQUIRE(waitFor([&] { return done; }));
    qunsetenv("PMP_FAKE_FFMPEG_EXIT");
    qunsetenv("PMP_FAKE_FFMPEG_ERR");
    REQUIRE_FALSE(ok);
    REQUIRE(message.contains("No space left on device"));
    REQUIRE(message.contains("kept"));
    REQUIRE(QFile::exists(out));
}

TEST_CASE("an encoder that dies early ends the recording and says so", "[gui][video]") {
    QTemporaryDir dir;
    qputenv("PMP_FAKE_FFMPEG_EARLY", "1");
    qputenv("PMP_FAKE_FFMPEG_ERR", "Unknown encoder");
    QWidget w;
    w.resize(160, 120);
    VideoRecorder rec;
    QString err, message;
    bool done = false;
    QObject::connect(&rec, &VideoRecorder::finished, &rec, [&](const QString&, bool, const QString& m) {
        done = true;
        message = m;
    });
    REQUIRE(rec.start(&w, dir.filePath("e.webm"), 30, fake(), &err));
    REQUIRE(waitFor([&] { return done; }));
    qunsetenv("PMP_FAKE_FFMPEG_EARLY");
    qunsetenv("PMP_FAKE_FFMPEG_ERR");
    REQUIRE(message.contains("stopped early"));
    REQUIRE(message.contains("Unknown encoder"));
    REQUIRE_FALSE(rec.recording());
    REQUIRE_FALSE(rec.timerActive());
}

TEST_CASE("an output path is always absolute and never starts with a dash", "[gui][video]") {
    REQUIRE(VideoRecorder::safeOutputPath("-evil.webm").startsWith('/'));
    REQUIRE_FALSE(VideoRecorder::safeOutputPath("-evil.webm").startsWith('-'));
    REQUIRE(VideoRecorder::safeOutputPath("/tmp/a b.webm") == "/tmp/a b.webm");
}

TEST_CASE("grabbing the real window yields a picture that is not one flat colour", "[gui][video]") {
    MainWindow w;
    w.resize(900, 600);
    w.show();
    const QImage img = w.grab().toImage();
    QSet<QRgb> colours;
    for (int y = 0; y < img.height(); y += 7) {
        for (int x = 0; x < img.width(); x += 7) colours.insert(img.pixel(x, y));
    }
    REQUIRE(colours.size() > 3);
}
