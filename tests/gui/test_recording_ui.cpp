// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/trace/player.hpp>

#include "trace/trace_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenuBar>
#include <QMimeData>
#include <QThread>
#include <QEventLoop>
#include <QTemporaryDir>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::testing;

namespace {

FrameUpdate update(int sec, bool gap = false) { return {std::make_shared<const Frame>(noisyFrame(sec)), gap}; }

struct Answers {
    QString savePath;
    QString openPath;
    int unsaved = 2;
    int unsavedAsked = 0;
};

void hook(MainWindow& w, Answers& a) {
    FileHooks h;
    h.savePath = [&a](const QString&) { return a.savePath; };
    h.openPath = [&a] { return a.openPath; };
    h.unsavedChoice = [&a](const QString&) {
        ++a.unsavedAsked;
        return a.unsaved;
    };
    w.setFileHooks(h);
}

}  // namespace

TEST_CASE("the footer button starts and stops a recording and pulses only while recording", "[gui][recording][ui]") {
    MainWindow w;
    REQUIRE_FALSE(w.footer()->pulseActive());  // NFR-04: nothing runs while idle
    w.footer()->recordButton()->click();
    REQUIRE(w.recording()->recording());
    REQUIRE(w.footer()->recording());
    REQUIRE(w.footer()->pulseActive());
    for (int i = 0; i < 4; ++i) w.applyFrame(update(i));
    REQUIRE(w.footer()->timecodeText() == "00:00:03.00");
    REQUIRE_FALSE(w.footer()->sizeText().isEmpty());  // FLR-09
    REQUIRE(w.saveAction()->isEnabled());
    w.footer()->recordButton()->click();
    REQUIRE_FALSE(w.footer()->pulseActive());
    REQUIRE_FALSE(w.footer()->recording());
}

TEST_CASE("a recording that reaches its limit says so", "[gui][recording][ui]") {
    AppSettings s;
    s.recordingMaxMinutes = 5;
    MainWindow w(s);
    w.recording()->start();
    for (int i = 0; i < 305; ++i) w.applyFrame(update(i));
    REQUIRE(w.noticeText() == "Recording stopped: reached the 5 minute limit. Save it to keep it.");
    REQUIRE_FALSE(w.footer()->recording());
}

TEST_CASE("saving through the menu writes the file and remembers it", "[gui][recording][ui]") {
    QTemporaryDir dir;
    MainWindow w;
    Answers a;
    a.savePath = dir.filePath("take1");  // no extension: one is added
    hook(w, a);
    int changed = 0;
    QObject::connect(&w, &MainWindow::settingsChanged, &w, [&] { ++changed; });
    REQUIRE_FALSE(w.saveAction()->isEnabled());
    w.recording()->start();
    for (int i = 0; i < 35; ++i) w.applyFrame(update(i));
    w.recording()->stop();
    w.saveAction()->trigger();
    REQUIRE(QFileInfo::exists(dir.filePath("take1.pmptrace")));
    REQUIRE(w.noticeText() == "Saved take1.pmptrace");
    REQUIRE(w.settings().recentRecordings == QStringList{dir.filePath("take1.pmptrace")});
    REQUIRE(changed >= 1);
    REQUIRE(w.recentMenu()->actions().size() == 1);
    REQUIRE_FALSE(w.recording()->dirty());
}

TEST_CASE("a failed save is reported and the recording is kept", "[gui][recording][ui]") {
    MainWindow w;
    Answers a;
    a.savePath = "/no/such/dir/x.pmptrace";
    hook(w, a);
    w.recording()->start();
    for (int i = 0; i < 3; ++i) w.applyFrame(update(i));
    w.recording()->stop();
    REQUIRE_FALSE(w.saveRecording());
    REQUIRE(w.noticeText().startsWith("Could not save the recording: "));
    REQUIRE(w.recording()->hasRecording());
    a.savePath = "";  // cancelled: no message, no change
    w.dismissNotice();
    REQUIRE_FALSE(w.saveRecording());
    REQUIRE(w.noticeText().isEmpty());
}

TEST_CASE("closing with an unsaved recording asks", "[gui][recording][ui]") {
    QTemporaryDir dir;
    {
        MainWindow w;
        Answers a;
        a.unsaved = 2;  // Cancel
        hook(w, a);
        w.recording()->start();
        for (int i = 0; i < 3; ++i) w.applyFrame(update(i));
        w.recording()->stop();
        REQUIRE_FALSE(w.close());
        REQUIRE(a.unsavedAsked == 1);
        a.unsaved = 1;  // Discard
        REQUIRE(w.close());
    }
    {
        MainWindow w;
        Answers a;
        a.unsaved = 0;  // Save…
        a.savePath = dir.filePath("closing.pmptrace");
        hook(w, a);
        w.recording()->start();
        for (int i = 0; i < 3; ++i) w.applyFrame(update(i));
        w.recording()->stop();
        REQUIRE(w.close());
        REQUIRE(QFileInfo::exists(dir.filePath("closing.pmptrace")));
    }
    {
        MainWindow w;  // nothing recorded: no question
        Answers a;
        hook(w, a);
        REQUIRE(w.close());
        REQUIRE(a.unsavedAsked == 0);
    }
}

TEST_CASE("ring mode: the button saves the last minutes and closing never asks", "[gui][recording][ui]") {
    QTemporaryDir dir;
    AppSettings s;
    s.recordingRing = true;
    s.recordingRingMinutes = 1;
    MainWindow w(s);
    Answers a;
    a.savePath = dir.filePath("black-box.pmptrace");
    hook(w, a);
    REQUIRE(w.recording()->recording());  // started by itself
    REQUIRE(w.saveLastAction()->text() == "Save Last 1 Minutes…");
    for (int i = 0; i < 150; ++i) w.applyFrame(update(i));
    REQUIRE(w.saveLastAction()->isEnabled());
    w.footer()->recordButton()->click();  // saves, does not stop
    REQUIRE(QFileInfo::exists(dir.filePath("black-box.pmptrace")));
    REQUIRE(w.recording()->recording());
    trace::Player p;
    REQUIRE(p.open(dir.filePath("black-box.pmptrace").toStdString()).ok());
    REQUIRE(p.sampleCount() >= 60);
    REQUIRE(p.sampleCount() <= 120);
    REQUIRE(w.close());
    REQUIRE(a.unsavedAsked == 0);
}

TEST_CASE("changing the settings turns the ring on and off", "[gui][recording][ui]") {
    MainWindow w;
    REQUIRE_FALSE(w.recording()->recording());
    AppSettings s = w.settings();
    s.recordingRing = true;
    w.applySettings(s);
    REQUIRE(w.recording()->recording());
    s.recordingRing = false;
    w.applySettings(s);
    REQUIRE_FALSE(w.recording()->recording());
}

TEST_CASE("dropping a non-trace file shows an error and changes nothing", "[gui][recording][ui]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("picture.png");
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("\x89PNG\r\n\x1a\n not a trace");
    }
    MainWindow w;
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&w, &enter);
    REQUIRE(enter.isAccepted());  // one local file is welcome
    QDropEvent drop(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&w, &drop);
    delete mime;
    for (int i = 0; i < 300 && w.noticeText().isEmpty(); ++i) {  // the file is read off the UI thread
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    REQUIRE(w.noticeText() == "This is not a PerfMonPlus recording.");
    REQUIRE(w.settings().recentRecordings.isEmpty());
}

TEST_CASE("notices are plain text", "[gui][recording][ui]") {
    MainWindow w;
    w.showNotice("<b>bold</b> & more");
    REQUIRE(w.noticeText() == "<b>bold</b> & more");
}

TEST_CASE("the shortcuts exist", "[gui][recording][ui]") {
    MainWindow w;
    REQUIRE(w.findChild<QAction*>("actionOpen")->shortcut() == QKeySequence("Ctrl+O"));
    REQUIRE(w.saveAction()->shortcut() == QKeySequence("Ctrl+S"));
}

TEST_CASE("the File menu bar is visible in the window", "[gui][recording][ui]") {
    MainWindow w;
    w.show();
    REQUIRE(w.menuBar()->isVisible());
    REQUIRE(w.menuBar()->height() > 10);
    REQUIRE(w.menuBar()->actions().first()->text() == "&File");
}

TEST_CASE("a file name is never rendered as rich text in the status line or the recent menu", "[gui][recording][ui][plain]") {
    QTemporaryDir dir;
    MainWindow w;
    w.footer()->setMessage("Replaying <b>x</b>.pmptrace — recorded today");
    REQUIRE(w.footer()->messageFormat() == Qt::PlainText);
    REQUIRE(w.footer()->text() == "Replaying <b>x</b>.pmptrace — recorded today");
    Answers a;
    a.savePath = dir.filePath("a&b.pmptrace");
    hook(w, a);
    w.recording()->start();
    for (int i = 0; i < 3; ++i) w.applyFrame(update(i));
    w.recording()->stop();
    REQUIRE(w.saveRecording());
    bool found = false;
    for (auto* act : w.recentMenu()->actions()) found = found || act->text() == "a&&b.pmptrace";
    REQUIRE(found);  // an ampersand in a name must not become a keyboard mnemonic
}
