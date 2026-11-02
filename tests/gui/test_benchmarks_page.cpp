// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/benchmarks_page.hpp>
#include <perfmonplus/gui/main_window.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::linux;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

struct FakeState {
    std::mutex m;
    std::vector<BenchId> ran;
    std::map<BenchId, std::string> refuse;
    std::atomic<bool> hold{false};
    std::atomic<int> holdOnly{-1};  // >= 0: only this test hangs until cancelled
    std::atomic<int> cleanups{0};
    std::uint64_t diskBytes = 1ull << 30;
};

class FakeRunner final : public BenchRunner {
public:
    explicit FakeRunner(std::shared_ptr<FakeState> s) : s_(std::move(s)) {}
    std::string preflight(BenchId id) override {
        const auto it = s_->refuse.find(id);
        return it == s_->refuse.end() ? std::string() : it->second;
    }
    std::uint64_t estimate(BenchId) override { return 6; }
    BenchResult run(BenchId id, const std::atomic<bool>& cancel, const std::function<void(double)>& progress) override {
        {
            std::lock_guard<std::mutex> lock(s_->m);
            s_->ran.push_back(id);
        }
        BenchResult r;
        r.id = id;
        r.rawUnit = rawUnitOf(id);
        for (int i = 0; i < 4; ++i) {
            progress(i / 4.0);
            while (s_->hold && (s_->holdOnly < 0 || s_->holdOnly == static_cast<int>(id)) && !cancel) std::this_thread::sleep_for(5ms);
            if (cancel) {
                r.cancelled = true;
                return r;
            }
            std::this_thread::sleep_for(5ms);
        }
        r.raw = 1000;
        r.score = scoreFor(id, r.raw);
        r.seconds = 0.1;
        return r;
    }
    void cleanup() override { ++s_->cleanups; }
    std::string tempDir() override { return "/home/test/.cache/perfmonplus"; }
    std::uint64_t diskBytes() override { return s_->diskBytes; }

private:
    std::shared_ptr<FakeState> s_;
};

fs::path tempFile() {
    static int n = 0;
    const auto d = fs::temp_directory_path() / ("pmp-benchpage-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
    fs::create_directories(d);
    return d / "benchmarks.json";
}

template <class Cond>
bool waitFor(Cond cond, int ms = 4000) {
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

struct Setup {
    std::shared_ptr<FakeState> state = std::make_shared<FakeState>();
    fs::path file = tempFile();
    int asked = 0;
    bool answer = true;
    QString text;
    std::unique_ptr<BenchmarksPage> page;
    explicit Setup(std::vector<BenchId> only = {}) {
        auto st = state;
        page = std::make_unique<BenchmarksPage>([st] { return std::make_unique<FakeRunner>(st); }, BenchStore(file));
        BenchmarksPage::Hooks h;
        h.confirmStart = [this](const QString&, const QString& t) {
            ++asked;
            text = t;
            return answer;
        };
        page->setHooks(h);
        if (!only.empty()) {
            for (int i = 0; i < kBenchCount; ++i) {
                const auto id = static_cast<BenchId>(i);
                page->checkFor(id)->setChecked(std::find(only.begin(), only.end(), id) != only.end());
            }
        }
    }
};

}  // namespace

TEST_CASE("the warning comes first and names what will happen", "[gui][bench-page]") {
    Setup s;
    s.page->update(Frame{}, HistoryStore(10), MonoTime{});
    s.page->startButton()->click();
    REQUIRE(s.asked == 1);
    REQUIRE(s.text.contains("will use all CPU cores and may make your computer warm and slow for a short time"));
    REQUIRE(s.text.contains("about 42 seconds"));  // 7 tests x 6 s each
    REQUIRE(s.text.contains("1.1 GB test file in /home/test/.cache/perfmonplus"));
    REQUIRE_FALSE(s.text.contains("battery"));
    REQUIRE(s.text.contains("keeps sampling"));
    REQUIRE(s.state->ran.empty());  // nothing started until the answer
    REQUIRE(waitFor([&] { return s.page->running(); }));
    REQUIRE(waitFor([&] { return !s.page->running(); }));
}

TEST_CASE("declining runs nothing", "[gui][bench-page]") {
    Setup s;
    s.answer = false;
    s.page->startButton()->click();
    REQUIRE(s.asked == 1);
    waitFor([] { return false; }, 100);
    REQUIRE_FALSE(s.page->running());
    REQUIRE(s.state->ran.empty());
}

TEST_CASE("the battery is mentioned when running on battery", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle});
    Frame f;
    f.energy.source = PowerSource::Battery;
    s.page->update(f, HistoryStore(10), MonoTime{});
    s.page->startButton()->click();
    REQUIRE(s.text.contains("You are on battery power."));
    REQUIRE_FALSE(s.text.contains("test file"));  // a CPU-only run writes nothing
    REQUIRE(s.text.contains("about 6 seconds"));
    waitFor([&] { return !s.page->running(); });
}

TEST_CASE("choosing tests", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle, BenchId::MemLatency});
    s.page->startButton()->click();
    REQUIRE(waitFor([&] { return !s.page->running() && s.state->ran.size() == 2; }));
    REQUIRE(s.state->ran == std::vector<BenchId>{BenchId::CpuSingle, BenchId::MemLatency});
    REQUIRE(s.page->gpuCheck()->isChecked() == false);
    REQUIRE_FALSE(s.page->gpuCheck()->isEnabled());
    REQUIRE(s.page->gpuCheck()->text().contains("not available in this build"));
    // nothing ticked: nothing to start
    for (int i = 0; i < kBenchCount; ++i) s.page->checkFor(static_cast<BenchId>(i))->setChecked(false);
    REQUIRE_FALSE(s.page->startButton()->isEnabled());
}

TEST_CASE("progress is live and the run finishes with cards, history and a saved file", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle, BenchId::DiskSeqWrite});
    s.page->startButton()->click();
    REQUIRE(waitFor([&] { return s.page->running(); }));
    REQUIRE(s.page->cancelButton()->isHidden() == false);
    REQUIRE_FALSE(s.page->startButton()->isEnabled());
    REQUIRE(waitFor([&] { return !s.page->currentText().isEmpty(); }));
    REQUIRE(waitFor([&] { return !s.page->running(); }));
    REQUIRE(s.page->progressBar()->value() == 1000);
    REQUIRE(s.page->statusText() == "Done.");
    REQUIRE(s.page->cards().size() == 2);
    REQUIRE(s.page->cards()[0].name == "CPU single-thread");
    REQUIRE(s.page->cards()[0].raw == "1,000 Mops/s");
    REQUIRE(s.page->cardFrame(0) != nullptr);
    REQUIRE(s.page->historyList()->count() == 1);
    REQUIRE(BenchStore(s.file).load().size() == 1);
    REQUIRE(s.state->cleanups.load() >= 1);
    REQUIRE(s.page->cards()[0].comparison == "First run on this machine");
}

TEST_CASE("a second run is compared with the first", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle});
    for (int i = 0; i < 2; ++i) {
        s.page->startButton()->click();
        REQUIRE(waitFor([&] { return s.page->running(); }));
        REQUIRE(waitFor([&] { return !s.page->running(); }));
        QThread::msleep(5);  // distinct finish times
    }
    REQUIRE(s.page->historyList()->count() == 2);
    REQUIRE(s.page->cards()[0].comparison.starts_with("No change vs your run on "));
}

TEST_CASE("cancel stops promptly and keeps nothing for the cancelled test", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle, BenchId::CpuMulti});
    s.state->hold = true;
    s.page->startButton()->click();
    REQUIRE(waitFor([&] { return s.page->running(); }));
    REQUIRE(waitFor([&] { return !s.state->ran.empty(); }));
    const auto t0 = std::chrono::steady_clock::now();
    s.page->cancelButton()->click();
    REQUIRE(waitFor([&] { return !s.page->running(); }, 2000));
    REQUIRE(std::chrono::steady_clock::now() - t0 < 300ms + 100ms);
    REQUIRE(s.state->ran.size() == 1);  // the second test never started
    REQUIRE(s.page->statusText() == "Cancelled.");
    REQUIRE(s.page->historyList()->count() == 0);  // nothing finished, so nothing was recorded
    REQUIRE(BenchStore(s.file).load().empty());
}

TEST_CASE("cancelling after some tests finished keeps those", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle, BenchId::CpuMulti});
    s.state->hold = true;
    s.state->holdOnly = static_cast<int>(BenchId::CpuMulti);  // the second test hangs until cancelled; the first runs through
    s.page->startButton()->click();
    REQUIRE(waitFor([&] { return s.state->ran.size() == 2; }));
    s.page->cancel();
    REQUIRE(waitFor([&] { return !s.page->running(); }));
    REQUIRE(s.page->statusText() == "Cancelled: the tests that finished were kept.");
    const auto runs = BenchStore(s.file).load();
    REQUIRE(runs.size() == 1);
    REQUIRE(runs[0].results.size() == 1);
    REQUIRE(runs[0].results[0].id == BenchId::CpuSingle);
}

TEST_CASE("a refused preflight shows the reason and starts nothing", "[gui][bench-page]") {
    Setup s({BenchId::CpuSingle, BenchId::DiskSeqWrite});
    s.state->refuse[BenchId::DiskSeqWrite] = "Needs 2.1 GB free in /x; only 0.3 GB is available.";
    s.page->startButton()->click();
    REQUIRE(s.page->noticeText() == "Disk sequential write: Needs 2.1 GB free in /x; only 0.3 GB is available.");
    REQUIRE(s.asked == 0);  // no warning for a run that cannot happen
    REQUIRE_FALSE(s.page->running());
    waitFor([] { return false; }, 100);
    REQUIRE(s.state->ran.empty());  // not even the CPU test
}

TEST_CASE("a disk scan in progress blocks a run", "[gui][bench-page]") {
    Setup s;
    s.page->setRunBlocked(true, "A disk scan is running.");
    REQUIRE_FALSE(s.page->startButton()->isEnabled());
    REQUIRE(s.page->startButton()->toolTip() == "A disk scan is running.");
    s.page->start();
    REQUIRE(s.asked == 0);
    s.page->setRunBlocked(false, {});
    REQUIRE(s.page->startButton()->isEnabled());
}

TEST_CASE("destroying the page mid-run cancels it and cleans up", "[gui][bench-page]") {
    auto st = std::make_shared<FakeState>();
    st->hold = true;
    {
        BenchmarksPage page([st] { return std::make_unique<FakeRunner>(st); }, BenchStore(tempFile()));
        BenchmarksPage::Hooks h;
        h.confirmStart = [](const QString&, const QString&) { return true; };
        page.setHooks(h);
        page.startButton()->click();
        REQUIRE(waitFor([&] { return !st->ran.empty(); }));
    }  // the destructor cancels, joins and removes the temp file
    REQUIRE(st->cleanups.load() >= 1);
}

TEST_CASE("a damaged history file is reported", "[gui][bench-page]") {
    const auto f = tempFile();
    std::ofstream(f) << "{ nope";
    BenchmarksPage page([] { return std::make_unique<FakeRunner>(std::make_shared<FakeState>()); }, BenchStore(f));
    REQUIRE(page.storeWarning().contains("was damaged"));
    REQUIRE(page.noticeText().contains("was damaged"));
}

TEST_CASE("starting a run pauses connections polling and blocks disk scans", "[gui][bench-page]") {
    MainWindow w;
    auto* bench = dynamic_cast<BenchmarksPage*>(w.pageWidget(Page::Benchmarks));
    REQUIRE(bench != nullptr);
    emit bench->runningChanged(true);
    // the window wires the other heavy pages to this signal
    auto* disk = w.pageWidget(Page::DiskSpace);
    REQUIRE(disk->findChild<QPushButton*>() != nullptr);
    bool disabledScan = false;
    for (auto* b : disk->findChildren<QPushButton*>()) {
        if (b->text() == "Scan") disabledScan = !b->isEnabled();
    }
    REQUIRE(disabledScan);
    emit bench->runningChanged(false);
    for (auto* b : disk->findChildren<QPushButton*>()) {
        if (b->text() == "Scan") REQUIRE(b->isEnabled());
    }
}

TEST_CASE("a debug build says its scores are not comparable", "[gui][bench-page][minor]") {
    Setup s;
    if (linux::kOptimizedBuild) REQUIRE(s.page->noticeText().isEmpty());
    else REQUIRE(s.page->noticeText().contains("debug build"));
}
