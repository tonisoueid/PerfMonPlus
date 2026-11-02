// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/recording_controller.hpp>
#include <perfmonplus/trace/player.hpp>

#include "trace/trace_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::testing;
using namespace std::chrono_literals;

namespace {

FrameUpdate update(int sec, bool gap = false) {
    return {std::make_shared<const Frame>(noisyFrame(sec)), gap};
}

}  // namespace

TEST_CASE("timecode formats as HH:MM:SS.ff", "[gui][recording]") {
    REQUIRE(RecordingController::formatTimecode(3'723'450ms) == "01:02:03.45");
    REQUIRE(RecordingController::formatTimecode(0ms) == "00:00:00.00");
    REQUIRE(RecordingController::formatTimecode(-5ms) == "00:00:00.00");
    REQUIRE(RecordingController::formatTimecode(36'000'999ms) == "10:00:00.99");
}

TEST_CASE("size text", "[gui][recording]") {
    REQUIRE(RecordingController::formatSize(68'000) == "≈ 68 KB");
    REQUIRE(RecordingController::formatSize(10) == "≈ 1 KB");
    REQUIRE(RecordingController::formatSize(1'450'000) == "≈ 1.4 MB");
}

TEST_CASE("the record button starts and stops a recording", "[gui][recording]") {
    RecordingController c;
    c.configure(AppSettings{}, 1000ms);
    REQUIRE_FALSE(c.recording());
    c.toggle();
    REQUIRE(c.recording());
    int progressCount = 0;
    QObject::connect(&c, &RecordingController::progress, &c, [&] { ++progressCount; });
    for (int i = 0; i < 5; ++i) c.onFrame(update(i));
    REQUIRE(c.sampleCount() == 5);
    REQUIRE(c.timecode() == "00:00:04.00");
    REQUIRE(progressCount == 5);
    REQUIRE_FALSE(c.sizeText().isEmpty());
    c.toggle();
    REQUIRE_FALSE(c.recording());
    REQUIRE(c.hasRecording());
    REQUIRE(c.dirty());
    c.onFrame(update(9));
    REQUIRE(c.sampleCount() == 5);  // stopped: frames are ignored
}

TEST_CASE("a recording that stops itself says so", "[gui][recording]") {
    AppSettings s;
    s.recordingMaxMinutes = 5;
    RecordingController c;
    c.configure(s, 1000ms);
    int limitCount = 0, limitMinutes = 0;
    QObject::connect(&c, &RecordingController::limitReached, &c, [&](int m) { ++limitCount; limitMinutes = m; });
    c.start();
    for (int i = 0; i < 400; ++i) c.onFrame(update(i));
    REQUIRE_FALSE(c.recording());
    REQUIRE(limitCount == 1);
    REQUIRE(limitMinutes == 5);
    REQUIRE(c.sampleCount() == 300);
}

TEST_CASE("saving writes a file the player opens, and clears the dirty flag", "[gui][recording]") {
    QTemporaryDir dir;
    RecordingController c;
    c.configure(AppSettings{}, 1000ms);
    c.start();
    for (int i = 0; i < 40; ++i) c.onFrame(update(i));
    c.stop();
    QString err;
    const auto path = dir.filePath("a.pmptrace");
    REQUIRE(c.save(path, &err));
    REQUIRE_FALSE(c.dirty());
    trace::Player p;
    REQUIRE(p.open(path.toStdString()).ok());
    REQUIRE(p.sampleCount() == 40);
}

TEST_CASE("a failed save is reported, not swallowed", "[gui][recording]") {
    RecordingController c;
    c.configure(AppSettings{}, 1000ms);
    c.start();
    for (int i = 0; i < 3; ++i) c.onFrame(update(i));
    c.stop();
    QString err;
    REQUIRE_FALSE(c.save("/no/such/dir/x.pmptrace", &err));
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE(c.hasRecording());  // still in memory
    REQUIRE(c.dirty());
    QString err2;
    RecordingController empty;
    REQUIRE_FALSE(empty.save("/tmp/x.pmptrace", &err2));
    REQUIRE(err2 == "There is nothing recorded to save.");
}

TEST_CASE("ring mode records continuously and saves the last N minutes", "[gui][recording]") {
    AppSettings s;
    s.recordingRing = true;
    s.recordingRingMinutes = 1;
    RecordingController c;
    c.configure(s, 1000ms);
    REQUIRE(c.recording());  // no button press needed
    REQUIRE(c.ring());
    for (int i = 0; i < 200; ++i) c.onFrame(update(i));
    REQUIRE_FALSE(c.dirty());  // ring buffers never ask to be saved
    QTemporaryDir dir;
    QString err;
    REQUIRE(c.save(dir.filePath("last.pmptrace"), &err, 1));
    trace::Player p;
    REQUIRE(p.open(dir.filePath("last.pmptrace").toStdString()).ok());
    REQUIRE(p.sampleCount() >= 60);
    REQUIRE(p.sampleCount() <= 120);
    // leaving ring mode drops the buffer
    c.configure(AppSettings{}, 1000ms);
    REQUIRE_FALSE(c.recording());
    REQUIRE_FALSE(c.hasRecording());
}

TEST_CASE("gaps are passed through", "[gui][recording]") {
    RecordingController c;
    c.configure(AppSettings{}, 1000ms);
    c.start();
    c.onFrame(update(0));
    c.onFrame(update(30, true));
    c.stop();
    QTemporaryDir dir;
    QString err;
    REQUIRE(c.save(dir.filePath("g.pmptrace"), &err));
    trace::Player p;
    REQUIRE(p.open(dir.filePath("g.pmptrace").toStdString()).ok());
    REQUIRE(p.gapBefore(1));
}

TEST_CASE("changing the interval or the buffer length keeps what the rolling buffer holds", "[gui][recording][minor]") {
    AppSettings s;
    s.recordingRing = true;
    s.recordingRingMinutes = 10;
    RecordingController c;
    c.configure(s, 1000ms);
    for (int i = 0; i < 100; ++i) c.onFrame(update(i));
    REQUIRE(c.sampleCount() == 100);
    s.recordingRingMinutes = 20;  // a longer window
    c.configure(s, 2000ms);       // and a slower sampling interval
    REQUIRE(c.recording());
    REQUIRE(c.sampleCount() == 100);  // nothing was thrown away
    for (int i = 100; i < 110; ++i) c.onFrame(update(i));
    REQUIRE(c.sampleCount() == 110);
    s.recordingRingMinutes = 1;  // a shorter window trims the oldest, and only the oldest
    c.configure(s, 2000ms);
    for (int i = 110; i < 200; ++i) c.onFrame(update(i));
    REQUIRE(c.sampleCount() < 110 + 90);
    REQUIRE(c.sampleCount() >= 60);
}
