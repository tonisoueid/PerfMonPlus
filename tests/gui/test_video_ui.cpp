// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/settings_dialog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QEventLoop>
#include <QSettings>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

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

VideoSupport fakeSupport() {
    VideoSupport s;
    s.ok = true;
    s.ffmpeg = PMP_FAKE_FFMPEG;
    s.encoder = "libvpx-vp9";
    s.container = "webm";
    return s;
}

}  // namespace

TEST_CASE("without ffmpeg the menu action says what to install", "[gui][video-ui]") {
    MainWindow w;
    VideoSupport none;
    none.reason = "Video recording needs ffmpeg with the VP9 or H.264 encoder. Install the 'ffmpeg' package, then try again.";
    w.setVideoSupportProbe([none] { return none; });
    int asked = 0;
    FileHooks h;
    h.videoPath = [&](const QString&) {
        ++asked;
        return QString("/tmp/x.webm");
    };
    w.setFileHooks(h);
    w.videoAction()->trigger();
    REQUIRE(w.noticeText() == none.reason);
    REQUIRE(asked == 0);  // no file dialog for something that cannot work
    REQUIRE_FALSE(w.videoRecording());
    REQUIRE_FALSE(w.footer()->videoVisible());
}

TEST_CASE("recording shows a blinking indicator and the elapsed time, then saves", "[gui][video-ui]") {
    QTemporaryDir dir;
    MainWindow w;
    w.resize(800, 500);
    w.show();
    w.setVideoSupportProbe([] { return fakeSupport(); });
    FileHooks h;
    h.videoPath = [&](const QString& suggested) {
        REQUIRE(suggested.endsWith(".webm"));
        return dir.filePath("take");  // no extension: the container's is added
    };
    w.setFileHooks(h);
    REQUIRE_FALSE(w.footer()->videoBlinkActive());  // NFR-04: nothing runs while idle
    w.videoAction()->trigger();
    REQUIRE(w.videoRecording());
    REQUIRE(w.footer()->videoVisible());
    REQUIRE(w.footer()->videoBlinkActive());
    REQUIRE(w.footer()->videoText() == "● VIDEO 00:00");
    REQUIRE(w.footer()->videoToolTip().contains("extra CPU"));
    REQUIRE(w.videoAction()->text() == "Stop &Video Recording");
    REQUIRE(waitFor([&] { return w.footer()->videoText() != "● VIDEO 00:00"; }, 3000));
    w.videoAction()->trigger();  // stop
    REQUIRE(waitFor([&] { return !w.noticeText().isEmpty() && !w.videoRecording(); }));
    REQUIRE(w.noticeText() == "Saved take.webm");
    REQUIRE(QFile::exists(dir.filePath("take.webm")));
    REQUIRE_FALSE(w.footer()->videoVisible());
    REQUIRE_FALSE(w.footer()->videoBlinkActive());
    REQUIRE(w.videoAction()->text() == "Record &Video…");
}

TEST_CASE("the frame rate setting reaches the encoder", "[gui][video-ui]") {
    QTemporaryDir dir;
    AppSettings s;
    s.videoFps = 15;
    MainWindow w(s);
    w.resize(800, 500);
    w.show();
    w.setVideoSupportProbe([] { return fakeSupport(); });
    FileHooks h;
    h.videoPath = [&](const QString&) { return dir.filePath("slow.webm"); };
    w.setFileHooks(h);
    w.videoAction()->trigger();
    w.videoAction()->trigger();
    REQUIRE(waitFor([&] { return !w.videoRecording() && !w.noticeText().isEmpty(); }));
    QFile f(dir.filePath("slow.webm"));
    REQUIRE(f.open(QIODevice::ReadOnly));
    const auto lines = QString::fromUtf8(f.readAll()).split('\n');
    REQUIRE(lines.contains("15"));  // after "-r"
    REQUIRE(lines.indexOf("-r") >= 0);
    REQUIRE(lines[lines.indexOf("-r") + 1] == "15");
}

TEST_CASE("cancelling the file dialog starts nothing", "[gui][video-ui]") {
    MainWindow w;
    w.setVideoSupportProbe([] { return fakeSupport(); });
    FileHooks h;
    h.videoPath = [](const QString&) { return QString(); };
    w.setFileHooks(h);
    w.videoAction()->trigger();
    REQUIRE_FALSE(w.videoRecording());
    REQUIRE(w.noticeText().isEmpty());
}

TEST_CASE("the video settings are validated and edited", "[gui][video-ui][settings]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    store.setValue("videoFps", 45);
    REQUIRE(AppSettings::load(store).videoFps == 30);
    store.setValue("videoFps", 15);
    REQUIRE(AppSettings::load(store).videoFps == 15);
    AppSettings s;
    SettingsDialog dlg(s, {});
    REQUIRE(dlg.videoFpsCombo()->currentData().toInt() == 30);
    dlg.videoFpsCombo()->setCurrentIndex(0);
    REQUIRE(dlg.result().videoFps == 15);
}

TEST_CASE("closing the window during a recording stops it", "[gui][video-ui]") {
    QTemporaryDir dir;
    MainWindow w;
    w.resize(800, 500);
    w.show();
    w.setVideoSupportProbe([] { return fakeSupport(); });
    FileHooks h;
    h.videoPath = [&](const QString&) { return dir.filePath("closing.webm"); };
    w.setFileHooks(h);
    w.videoAction()->trigger();
    REQUIRE(w.videoRecording());
    w.close();
    REQUIRE_FALSE(w.videoRecording());
}
