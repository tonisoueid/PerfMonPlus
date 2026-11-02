// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/placeholder_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>
#include <QSettings>
#include <thread>
#include <QShortcut>
#include <perfmonplus/gui/performance_page.hpp>
#include <perfmonplus/gui/summary_page.hpp>
#include <QTemporaryDir>

using namespace pmp;
using namespace pmp::gui;

namespace {

Frame frameWith(int processes, std::uint64_t generation, int sec, std::vector<HealthState> health = {}) {
    Frame f;
    f.generation = generation;
    f.mono = MonoTime{} + std::chrono::seconds(sec);
    f.processes.count = Metric::measured(processes, Unit::Count);
    f.cpu.total = Metric::measured(10, Unit::Percent);
    int i = 0;
    for (auto s : health) {
        ProviderHealth h;
        h.id = "p" + std::to_string(i++);
        h.state = s;
        f.providerHealth.push_back(h);
    }
    return f;
}

FrameUpdate update(Frame f, bool gap = false) { return {std::make_shared<const Frame>(std::move(f)), gap}; }

}  // namespace

TEST_CASE("sidebar lists the destinations in order with a TOOLS divider", "[gui][shell]") {
    MainWindow w;
    const QStringList expected = {"Summary",       "Performance", "Processes",       "System Info", "Startup apps",
                                  "Users",         "Services",    "TOOLS",           "Power & Freq", "Connections",
                                  "Installed Apps", "Disk Space",  "Benchmarks"};
    REQUIRE(w.sidebarLabels() == expected);
}

TEST_CASE("selecting a page switches the stack, by API and by shortcut", "[gui][shell]") {
    MainWindow w;
    REQUIRE(w.currentPage() == Page::Summary);
    w.showPage(Page::Performance);
    REQUIRE(w.currentPage() == Page::Performance);
    w.showPage(Page::Summary);
    bool found = false;
    for (auto* s : w.findChildren<QShortcut*>()) {
        if (s->key() == QKeySequence("Ctrl+2")) {
            found = true;
            emit s->activated();
        }
    }
    REQUIRE(found);
    REQUIRE(w.currentPage() == Page::Performance);
}

TEST_CASE("every destination has a real page", "[gui][shell]") {
    MainWindow w;
    for (int i = 0; i < kPageCount; ++i) {
        INFO("page " << i);
        REQUIRE(dynamic_cast<PlaceholderPage*>(w.pageWidget(static_cast<Page>(i))) == nullptr);
    }
}

TEST_CASE("status bar reports healthy and counts", "[gui][shell]") {
    StatusBar bar;
    bar.update(frameWith(312, 122, 0, {HealthState::Healthy, HealthState::Healthy}));
    REQUIRE(bar.text() == "Native providers healthy · 312 processes · Generation 122");
    REQUIRE(bar.healthy());
}

TEST_CASE("status bar reports degraded providers", "[gui][shell]") {
    StatusBar bar;
    bar.update(frameWith(5, 7, 0, {HealthState::Healthy, HealthState::Failed}));
    REQUIRE(bar.text().startsWith("1 provider(s) degraded"));
    REQUIRE_FALSE(bar.healthy());
}

TEST_CASE("status bar never shows a made-up process count", "[gui][shell]") {
    StatusBar bar;
    Frame f;
    f.generation = 1;
    bar.update(f);
    REQUIRE(bar.text().contains("— processes"));
}

TEST_CASE("settings round-trip and defaults", "[gui][shell]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    AppSettings defaults = AppSettings::load(store);
    REQUIRE(defaults.themeName == "dark");
    REQUIRE(defaults.saturation == 7);
    REQUIRE(defaults.bloom);
    REQUIRE(defaults.frameRate == 60);
    REQUIRE(defaults.historySeconds == 60);
    REQUIRE_FALSE(defaults.sidebarCollapsed);
    REQUIRE(defaults.lastPage == 0);

    AppSettings s;
    s.themeName = "green";
    s.saturation = 3;
    s.bloom = false;
    s.frameRate = 30;
    s.lastPage = 1;
    s.sidebarCollapsed = true;
    s.save(store);
    store.sync();
    REQUIRE(AppSettings::load(store) == s);
}

TEST_CASE("window restores the last page", "[gui][shell]") {
    AppSettings s;
    s.lastPage = 1;
    MainWindow w(s);
    REQUIRE(w.currentPage() == Page::Performance);
}

TEST_CASE("applyFrame feeds history and the status bar", "[gui][shell]") {
    MainWindow w;
    w.applyFrame(update(frameWith(10, 1, 0, {HealthState::Healthy})));
    w.applyFrame(update(frameWith(10, 2, 1, {HealthState::Healthy})));
    REQUIRE(w.history().series(Series::CpuTotal).size() == 2);
    REQUIRE(w.statusText() == "Native providers healthy · 10 processes · Generation 2");
}

TEST_CASE("pages never squeeze their text below its natural size at the minimum window", "[gui][shell]") {
    MainWindow w;
    w.resize(900, 575);
    w.show();
    QApplication::processEvents();
    w.applyFrame(update(frameWith(300, 5, 0, {HealthState::Healthy})));
    for (Page p : {Page::Summary, Page::Performance}) {
        w.showPage(p);
        for (int pass = 0; pass < 4; ++pass) QApplication::processEvents();  // late content (system facts) re-lays out
        auto* page = w.pageWidget(p);
        INFO("page " << static_cast<int>(p) << " width " << page->width() << " min " << page->minimumSizeHint().width());
        REQUIRE(page->width() >= page->minimumSizeHint().width());
        REQUIRE(page->height() >= page->minimumSizeHint().height());
        for (auto* label : page->findChildren<QLabel*>()) {
            if (!label->isVisibleTo(page) || label->text().isEmpty()) continue;
            INFO("label: " << label->text().toStdString());
            REQUIRE(label->width() >= label->minimumSizeHint().width());
        }
    }
}

TEST_CASE("restoring a saved maximised geometry during construction is safe", "[gui][shell]") {
    // A saved geometry that includes a window state makes restoreGeometry() deliver change events
    // from inside the constructor, before any page exists.
    AppSettings s;
    {
        QWidget donor;
        donor.setWindowState(Qt::WindowMaximized);
        s.geometry = donor.saveGeometry();
    }
    REQUIRE_FALSE(s.geometry.isEmpty());
    MainWindow w(s);
    REQUIRE(w.currentPage() == Page::Summary);
    REQUIRE(w.pageWidget(Page::Summary) != nullptr);
}

TEST_CASE("closing the window reports its state instead of saving appearance itself", "[gui][shell]") {
    MainWindow w;
    std::optional<AppSettings> closing;
    QObject::connect(&w, &MainWindow::closing, &w, [&](AppSettings s) { closing = s; });
    w.showPage(Page::Performance);
    w.close();
    REQUIRE(closing);
    REQUIRE(closing->lastPage == 1);
    REQUIRE_FALSE(closing->geometry.isEmpty());
}

TEST_CASE("a bad saved frame rate or history length falls back to the defaults", "[gui][shell]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    for (int bad : {0, -5, 45, 9999}) {  // only 15/30/60/120 are choices in the Colors popover
        store.setValue("frameRate", bad);
        REQUIRE(AppSettings::load(store).frameRate == 60);
    }
    for (int bad : {0, -5, 9999}) {
        store.setValue("historySeconds", bad);
        REQUIRE(AppSettings::load(store).historySeconds == 60);
    }
    store.setValue("frameRate", 30);
    store.setValue("historySeconds", 300);
    const auto good = AppSettings::load(store);
    REQUIRE(good.frameRate == 30);
    REQUIRE(good.historySeconds == 300);
}

TEST_CASE("status bar says Starting before the first frame", "[gui][shell]") {
    StatusBar bar;
    REQUIRE(bar.text() == "Starting…");
    bar.update(frameWith(5, 1, 0, {HealthState::Healthy}));
    REQUIRE(bar.text().contains("Generation 1"));
}

TEST_CASE("clicking a Summary tile opens the matching Performance panel", "[gui][shell]") {
    MainWindow w;
    auto* summary = dynamic_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    auto* perf = dynamic_cast<PerformancePage*>(w.pageWidget(Page::Performance));
    REQUIRE(summary);
    REQUIRE(perf);
    summary->tiles().at(1)->click();  // Network
    REQUIRE(w.currentPage() == Page::Performance);
    REQUIRE(perf->detailTitle() == "Network");
    w.showPage(Page::Summary);
    summary->tiles().at(0)->click();  // Disks
    REQUIRE(perf->detailTitle() == "Disks");
}

TEST_CASE("background workers are counted and the app can wait for them", "[gui][shell][minor]") {
    std::atomic<bool> release{false};
    QObject context;
    REQUIRE(waitForAsyncWorkers(std::chrono::seconds(5)));  // whatever earlier tests started (system info reads) has drained
    runAsync<int>(&context, [&] {
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return 7;
    }, [](int) {});
    REQUIRE(asyncInFlight() == 1);
    REQUIRE_FALSE(waitForAsyncWorkers(std::chrono::milliseconds(30)));  // still busy: the wait gives up, it does not hang
    release = true;
    REQUIRE(waitForAsyncWorkers(std::chrono::seconds(2)));
    QCoreApplication::processEvents();
}

TEST_CASE("a narrow window hides the sidebar and a wide one brings it back", "[gui][shell][minor]") {
    MainWindow w;
    REQUIRE_FALSE(w.sidebarHidden());
    w.adaptToWidth(MainWindow::kNarrowWidth - 1);
    REQUIRE(w.sidebarHidden());
    REQUIRE_FALSE(w.settings().sidebarCollapsed);  // not the user's choice, so not saved
    w.adaptToWidth(MainWindow::kNarrowWidth);
    REQUIRE_FALSE(w.sidebarHidden());
}

TEST_CASE("a sidebar the user collapsed stays collapsed when the window grows", "[gui][shell][minor]") {
    AppSettings s;
    s.sidebarCollapsed = true;
    MainWindow w(s);
    w.adaptToWidth(MainWindow::kNarrowWidth - 1);
    w.adaptToWidth(MainWindow::kNarrowWidth + 200);
    REQUIRE(w.sidebarHidden());
}
