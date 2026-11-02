// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/gui/summary_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QLocale>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <fstream>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

QString golden() { return QString(PMP_TEST_DATA_DIR) + "/golden-1.pmptrace"; }

template <class Cond>
bool waitFor(Cond cond, int ms = 3000) {
    QEventLoop loop;
    QTimer poll;
    QTimer limit;
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

class TickProvider final : public Provider {
public:
    explicit TickProvider(std::shared_ptr<std::atomic<int>> calls) : calls_(std::move(calls)) {}
    std::string_view id() const override { return "tick"; }
    void sample(Frame& out, const SampleContext&) override {
        ++*calls_;
        out.cpu.total = Metric::measured(10.0, Unit::Percent);
    }
    void fillUnavailable(Frame& out, Availability why) const override { out.cpu.total = Metric::notMeasured(why, Unit::Percent); }

private:
    std::shared_ptr<std::atomic<int>> calls_;
};

void open(MainWindow& w, const QString& path = golden()) {
    w.openRecording(path);
    REQUIRE(waitFor([&] { return w.replaying(); }));
}

int gapSlots(const HistoryRing<double>& r) {
    int n = 0;
    for (std::size_t i = 0; i < r.size(); ++i) n += r.at(i).value ? 0 : 1;
    return n;
}

QShortcut* shortcutFor(MainWindow& w, const QKeySequence& k) {
    for (auto* s : w.findChildren<QShortcut*>()) {
        if (s->key() == k) return s;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("opening a trace enters replay and shows the banner", "[gui][replay]") {
    MainWindow w;
    open(w);
    REQUIRE(w.replayBar()->isVisible() == false);  // the window itself is not shown, but the bar is not explicitly hidden
    REQUIRE_FALSE(w.replayBar()->isHidden());
    const QString date = QLocale().toString(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000), QLocale::ShortFormat);
    REQUIRE(w.replayBar()->bannerText() == QString("Replaying golden-1.pmptrace — recorded %1 on macOS 15").arg(date));
    REQUIRE(w.statusText() == w.replayBar()->bannerText());
    REQUIRE(w.replay()->transport().count() == 20);
    REQUIRE(w.replayBar()->slider()->maximum() == 19);
    REQUIRE(w.settings().recentRecordings.first() == golden());
}

TEST_CASE("the seven panels render from the trace", "[gui][replay]") {
    MainWindow w;
    open(w);
    w.showPage(Page::Summary);
    w.replay()->seek(7);
    auto* summary = dynamic_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    REQUIRE(summary->cpuValueText().contains("27.5"));
    REQUIRE(w.replay()->history().series(Series::CpuTotal).size() == 8);
    w.replay()->seek(2);
    REQUIRE(summary->cpuValueText().contains("15.0"));
    REQUIRE(w.replay()->history().series(Series::CpuTotal).size() == 3);
    REQUIRE(w.replay()->processHistory().approxBytes() > 0);  // "who was guilty" works on a trace
}

TEST_CASE("scrubbing across a gap shows a gap", "[gui][replay]") {
    MainWindow w;
    open(w);
    w.replay()->seek(15);
    const auto& ring = w.replay()->history().series(Series::CpuTotal);
    REQUIRE(gapSlots(ring) == 1);       // the hole before sample 12, and nothing invented
    REQUIRE(ring.size() == 17);         // 16 samples + 1 gap marker
    REQUIRE_FALSE(ring.at(12).value.has_value());
    REQUIRE(ring.at(11).value.has_value());
    REQUIRE(ring.at(13).value.has_value());
    w.replay()->seek(5);
    REQUIRE(gapSlots(w.replay()->history().series(Series::CpuTotal)) == 0);
    REQUIRE(w.replay()->gapAtPosition() == false);
    w.replay()->seek(12);
    REQUIRE(w.replay()->gapAtPosition());
}

TEST_CASE("live-only pages are labelled not recorded", "[gui][replay]") {
    MainWindow w;
    w.showPage(Page::Processes);
    open(w);
    REQUIRE(w.pageIsGated(Page::Processes));
    REQUIRE(w.pageIsGated(Page::Services));
    REQUIRE(w.pageIsGated(Page::Connections));
    REQUIRE_FALSE(w.pageIsGated(Page::Summary));
    REQUIRE_FALSE(w.pageIsGated(Page::Performance));
    REQUIRE_FALSE(w.pageIsGated(Page::PowerFreq));
    REQUIRE(w.currentPage() == Page::Processes);
    w.returnToLive();
    REQUIRE_FALSE(w.pageIsGated(Page::Processes));
}

TEST_CASE("replay keys exist only while a recording is open", "[gui][replay]") {
    MainWindow w;
    auto* space = shortcutFor(w, Qt::Key_Space);
    REQUIRE(space != nullptr);
    REQUIRE_FALSE(space->isEnabled());
    open(w);
    REQUIRE(space->isEnabled());
    REQUIRE_FALSE(w.replay()->transport().playing());
    QMetaObject::invokeMethod(space, "activated");
    REQUIRE(w.replay()->transport().playing());
    QMetaObject::invokeMethod(space, "activated");
    REQUIRE_FALSE(w.replay()->transport().playing());
    QMetaObject::invokeMethod(shortcutFor(w, Qt::Key_Right), "activated");
    REQUIRE(w.replay()->transport().pos() == 1);
    QMetaObject::invokeMethod(shortcutFor(w, Qt::Key_Left), "activated");
    REQUIRE(w.replay()->transport().pos() == 0);
    QMetaObject::invokeMethod(shortcutFor(w, Qt::SHIFT | Qt::Key_Right), "activated");
    REQUIRE(w.replay()->transport().pos() == 10);
    QMetaObject::invokeMethod(shortcutFor(w, Qt::Key_End), "activated");
    REQUIRE(w.replay()->transport().pos() == 19);
    QMetaObject::invokeMethod(shortcutFor(w, Qt::Key_Home), "activated");
    REQUIRE(w.replay()->transport().pos() == 0);
    w.returnToLive();
    REQUIRE_FALSE(space->isEnabled());
}

TEST_CASE("playing advances the playhead and stops at the end", "[gui][replay]") {
    MainWindow w;
    open(w);
    w.replayBar()->speedBox()->setCurrentIndex(w.replayBar()->speedBox()->findData(8.0));
    w.replay()->setSpeed(8.0);
    w.replayBar()->playButton()->click();
    REQUIRE(w.replay()->transport().playing());
    REQUIRE(waitFor([&] { return w.replay()->transport().pos() >= 3; }, 2000));
    REQUIRE(waitFor([&] { return !w.replay()->transport().playing(); }, 6000));
    REQUIRE(w.replay()->transport().pos() == 19);
    REQUIRE(w.replayBar()->playButton()->text() == "▶");
}

TEST_CASE("the slider scrubs", "[gui][replay]") {
    MainWindow w;
    open(w);
    w.replayBar()->slider()->setValue(9);
    REQUIRE(w.replay()->transport().pos() == 9);
    REQUIRE(w.replayBar()->positionText() == "00:00:09.00 / 00:00:59.00");  // recorded time includes the 40 s gap
}

TEST_CASE("live sampling is paused while replaying and resumes without a spike", "[gui][replay]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<TickProvider>(calls));
    MainWindow w(AppSettings{}, std::make_unique<SamplingService>(20ms, std::move(ps)));
    REQUIRE(waitFor([&] { return w.history().series(Series::CpuTotal).size() >= 3; }));
    const auto liveBefore = w.history().series(Series::CpuTotal).size();
    open(w);
    waitFor([] { return false; }, 150);  // a frame already in flight
    const int paused = calls->load();
    waitFor([] { return false; }, 300);
    REQUIRE(calls->load() == paused);
    REQUIRE(w.history().series(Series::CpuTotal).size() <= liveBefore + 8);  // replay never wrote into the live history
    const auto sizeAtOpen = w.history().series(Series::CpuTotal).size();
    w.returnToLive();
    REQUIRE(waitFor([&] { return calls->load() > paused + 2; }));
    REQUIRE(w.history().series(Series::CpuTotal).size() > sizeAtOpen);
    REQUIRE(gapSlots(w.history().series(Series::CpuTotal)) >= 1);  // the pause shows as a gap, not a spike or a line
    REQUIRE_FALSE(w.replaying());
}

TEST_CASE("replay does not touch the live history", "[gui][replay]") {
    MainWindow w;
    Frame f;
    f.generation = 1;
    f.mono = MonoTime{} + 1s;
    f.cpu.total = Metric::measured(42.0, Unit::Percent);
    w.applyFrame({std::make_shared<const Frame>(f), false});
    REQUIRE(w.history().series(Series::CpuTotal).size() == 1);
    open(w);
    w.replay()->seek(10);
    w.applyFrame({std::make_shared<const Frame>(f), false});  // a live frame arriving mid-replay is dropped
    REQUIRE(w.history().series(Series::CpuTotal).size() == 1);
}

TEST_CASE("return to live restores the status line and hides the bar", "[gui][replay]") {
    MainWindow w;
    Frame f;
    f.generation = 5;
    f.mono = MonoTime{} + 1s;
    w.applyFrame({std::make_shared<const Frame>(f), false});
    const auto before = w.statusText();
    open(w);
    REQUIRE(w.statusText().startsWith("Replaying "));
    w.replayBar()->returnButton()->click();
    REQUIRE_FALSE(w.replaying());
    REQUIRE(w.replayBar()->isHidden());
    REQUIRE(w.statusText() == before);
}

TEST_CASE("opening a bad file shows the error and stays live", "[gui][replay]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("picture.png");
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("\x89PNG\r\n\x1a\n not a trace");
    }
    MainWindow w;
    w.openRecording(path);
    REQUIRE(waitFor([&] { return !w.noticeText().isEmpty(); }));
    REQUIRE(w.noticeText() == "This is not a PerfMonPlus recording.");
    REQUIRE_FALSE(w.replaying());
    REQUIRE(w.settings().recentRecordings.isEmpty());
}

TEST_CASE("a recording from a newer version is refused with its message", "[gui][replay]") {
    QTemporaryDir dir;
    QFile in(golden());
    REQUIRE(in.open(QIODevice::ReadOnly));
    QByteArray bytes = in.readAll();
    bytes[8] = 2;
    const auto path = dir.filePath("newer.pmptrace");
    QFile out(path);
    REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(bytes);
    out.close();
    MainWindow w;
    w.openRecording(path);
    REQUIRE(waitFor([&] { return !w.noticeText().isEmpty(); }));
    REQUIRE(w.noticeText().contains("newer version of PerfMonPlus"));
    REQUIRE_FALSE(w.replaying());
}

TEST_CASE("a truncated recording replays what survived and says so", "[gui][replay]") {
    QTemporaryDir dir;
    QFile in(golden());
    REQUIRE(in.open(QIODevice::ReadOnly));
    QByteArray bytes = in.readAll();
    bytes.truncate(bytes.size() - 48);  // the index and the footer are gone, the chunk is whole
    const auto path = dir.filePath("cut.pmptrace");
    QFile out(path);
    REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(bytes);
    out.close();
    MainWindow w;
    open(w, path);
    REQUIRE(w.replayBar()->noteText().startsWith("Recovered "));
    REQUIRE(w.replay()->transport().count() > 0);
}

TEST_CASE("a manual recording in progress blocks opening another", "[gui][replay]") {
    MainWindow w;
    w.recording()->start();
    w.openRecording(golden());
    REQUIRE(w.noticeText() == "Stop the recording before opening another one.");
    REQUIRE_FALSE(w.replaying());
}

TEST_CASE("seeking never moves the rings the graphs are bound to", "[gui][replay]") {
    MainWindow w;
    open(w);
    const auto* ring = &w.replay()->history().series(Series::CpuTotal);
    const auto* process = &w.replay()->processHistory();
    w.replay()->seek(15);
    w.replay()->seek(3);
    w.replay()->step(1);
    REQUIRE(&w.replay()->history().series(Series::CpuTotal) == ring);
    REQUIRE(&w.replay()->processHistory() == process);
    REQUIRE(ring->size() == 5);
    // and the Summary graphs really draw from it: the CPU line has points inside the visible window
    w.showPage(Page::Summary);
    w.replay()->seek(10);
    REQUIRE(ring->size() == 11);
}

TEST_CASE("a graph draws a ring whose times are small steady-clock values", "[gui][replay]") {
    HistoryRing<double> ring(100);
    for (int i = 0; i <= 10; ++i) ring.push(MonoTime{} + std::chrono::seconds(i), 10.0 + i * 2.5);
    HistoryGraph g;
    g.resize(300, 100);
    g.setFixedScale({0, 100});
    g.setWindow(60s);
    g.addSeries(&ring, vm::Rgb{0, 255, 0});
    g.setNow(MonoTime{} + 10s);
    const QImage img = g.grab().toImage();
    int green = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.green() > 200 && c.red() < 80 && c.blue() < 80) ++green;
        }
    }
    REQUIRE(green > 20);
}

TEST_CASE("the Summary CPU graph draws replayed history", "[gui][replay]") {
    MainWindow w;
    w.resize(1280, 800);
    w.show();
    open(w);
    w.showPage(Page::Summary);
    w.replay()->seek(10);
    auto* summary = dynamic_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    summary->tick(0.016);  // the animation clock moves graphs with the real clock; a replay must not follow it
    const QImage img = summary->cpuGraph()->grab().toImage();
    int green = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.green() > 180 && c.red() < 120 && c.blue() < 120) ++green;
        }
    }
    INFO("series " << summary->cpuGraph()->seriesCount() << " ring size " << w.replay()->history().series(Series::CpuTotal).size());
    REQUIRE(green > 20);
}

TEST_CASE("graph times convert back to the recorded wall clock", "[gui][replay]") {
    MainWindow w;
    open(w);
    w.replay()->seek(5);
    const auto mono = w.replay()->monoAtPosition();
    // the same conversion the hover tooltip and the culprit list use
    const auto wall = std::chrono::system_clock::now() - (std::chrono::steady_clock::now() - mono);
    const auto recorded = w.replay()->wallAtPosition();
    REQUIRE(std::chrono::abs(wall - recorded) < 100ms);
    // frames carry the same mapped time as the history rings
    REQUIRE(w.replay()->frame()->mono == mono);
    const auto& ring = w.replay()->history().series(Series::CpuTotal);
    REQUIRE(ring.at(ring.size() - 1).t == mono);
}

TEST_CASE("a recording's file name in the recent menu is shown literally, never as markup", "[gui][replay][minor]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString odd = dir.filePath("<b>bold.pmptrace");
    REQUIRE(QFile::copy(golden(), odd));
    MainWindow w;
    open(w, odd);
    REQUIRE_FALSE(w.recentMenu()->actions().isEmpty());
    const QString tip = w.recentMenu()->actions().first()->toolTip();
    REQUIRE(tip.contains("&lt;b&gt;bold"));
    REQUIRE_FALSE(tip.contains("<b>"));
}
