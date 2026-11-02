// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/version.hpp>
#include <perfmonplus/gui/disk_space_page.hpp>
#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/performance_page.hpp>
#include <perfmonplus/gui/processes_page.hpp>
#include <perfmonplus/gui/screensaver.hpp>
#include <perfmonplus/gui/settings_dialog.hpp>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QSettings>
#include <QTimer>

#include <time.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

double cpuSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

QString fileNameFor(pmp::gui::Page p) {
    QString s = pmp::gui::pageLabel(p).toLower();
    s.remove('&');
    return s.simplified().replace(' ', '-') + ".png";
}

}  // namespace

int main(int argc, char** argv) {
    int runSeconds = 0;
    QString screenshotDir;
    bool lowEffects = false;
    std::string themeOverride;
    std::string startPage, openRecording, scanPath, videoPath;
    bool autoPlay = false, autoRecord = false;
    int shotWidth = 1280, shotHeight = 800;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("perfmonplus %s\n", std::string(pmp::versionString()).c_str());
            return 0;
        }
        if (!std::strcmp(argv[i], "--run-seconds") && i + 1 < argc) runSeconds = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--screenshot-dir") && i + 1 < argc) screenshotDir = argv[++i];
        if (!std::strcmp(argv[i], "--low-effects")) lowEffects = true;
        if (!std::strcmp(argv[i], "--size") && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &shotWidth, &shotHeight);
        if (!std::strcmp(argv[i], "--theme") && i + 1 < argc) themeOverride = argv[++i];
        if (!std::strcmp(argv[i], "--page") && i + 1 < argc) startPage = argv[++i];
        if (!std::strcmp(argv[i], "--open-recording") && i + 1 < argc) openRecording = argv[++i];
        if (!std::strcmp(argv[i], "--scan") && i + 1 < argc) scanPath = argv[++i];  // Disk Space: scan this folder at launch
        if (!std::strcmp(argv[i], "--record-video") && i + 1 < argc) videoPath = argv[++i];  // record the window to this file at launch
        if (!std::strcmp(argv[i], "--play")) autoPlay = true;      // with --open-recording: start playing at once
        if (!std::strcmp(argv[i], "--record")) autoRecord = true;  // start a recording at launch (CPU gates)
    }

    QApplication app(argc, argv);
    QSettings store("PerfMonPlus", "PerfMonPlus");
    auto settings = pmp::gui::AppSettings::load(store);
    pmp::gui::AppSettings persisted = settings;  // what is written back: never the one-run overrides below
    if (!themeOverride.empty()) settings.themeName = themeOverride;
    if (lowEffects) {
        settings.lowEffects = true;
        settings.bloom = false;
        settings.frameRate = std::min(settings.frameRate, 30);
    }

    const bool shooting = !screenshotDir.isEmpty();
    auto service = std::make_unique<pmp::gui::SamplingService>(shooting ? std::chrono::milliseconds(300) : settings.startupInterval());
    pmp::gui::SamplingService* svc = service.get();
    pmp::gui::MainWindow window(settings, std::move(service));
    // A deliberate change in the Colors popover is the user's choice and is remembered; command-line
    // overrides, screenshot runs and timed runs are not.
    const bool ephemeral = shooting || runSeconds > 0;
    QObject::connect(&window, &pmp::gui::MainWindow::settingsChanged, &window, [&](pmp::gui::AppSettings changed) {
        persisted.takeUserChoices(changed);  // every choice from Colors and Settings, not window state
        persisted.save(store);
    });
    QObject::connect(&window, &pmp::gui::MainWindow::closing, &window, [&](pmp::gui::AppSettings closingState) {
        if (ephemeral) return;
        persisted.geometry = closingState.geometry;
        persisted.lastPage = closingState.lastPage;
        persisted.sidebarCollapsed = closingState.sidebarCollapsed;
        persisted.save(store);
    });
    if (!startPage.empty()) {  // for this run only: it is never saved
        for (int p = 0; p < pmp::gui::kPageCount; ++p) {
            if (fileNameFor(static_cast<pmp::gui::Page>(p)) == QString::fromStdString(startPage) + ".png") window.showPage(static_cast<pmp::gui::Page>(p));
        }
    }
    window.show();

    if (autoRecord || !videoPath.empty()) {
        // Diagnostic modes (CPU gates, a scripted video): nobody is there to answer a question, so nothing asks. The
        // recording is thrown away when the run ends.
        pmp::gui::FileHooks hooks;
        hooks.unsavedChoice = [](const QString&) { return 1; };  // Discard
        if (!videoPath.empty()) hooks.videoPath = [videoPath](const QString&) { return QString::fromStdString(videoPath); };
        window.setFileHooks(hooks);
    }
    if (autoRecord) window.recording()->start();
    if (!videoPath.empty()) QTimer::singleShot(500, &window, [&window] { window.toggleVideoRecording(); });
    if (!scanPath.empty()) {
        if (auto* disk = dynamic_cast<pmp::gui::DiskSpacePage*>(window.pageWidget(pmp::gui::Page::DiskSpace))) {
            window.showPage(pmp::gui::Page::DiskSpace);
            disk->scanPath(QString::fromStdString(scanPath));
            if (shooting) {
                QObject::connect(disk, &pmp::gui::DiskSpacePage::scanFinished, &app, [&, disk] {
                    QDir().mkpath(screenshotDir);
                    window.resize(shotWidth, shotHeight);
                    QTimer::singleShot(300, &app, [&, disk] {
                        window.grab().save(screenshotDir + "/disk-space-map.png");
                        disk->tabs()->setCurrentIndex(1);
                        QTimer::singleShot(200, &app, [&, disk] {
                            window.grab().save(screenshotDir + "/disk-space-largest.png");
                            app.quit();
                        });
                    });
                });
            }
        }
    }
    if (!openRecording.empty()) {
        window.openRecording(QString::fromStdString(openRecording));
        if (autoPlay) {
            QObject::connect(window.replay(), &pmp::gui::ReplayController::stateChanged, &window, [&] {
                if (window.replaying() && !window.replay()->transport().playing() && window.replay()->transport().pos() == 0) {
                    window.replay()->togglePlay();
                }
            });
        }
        if (shooting) {  // replay screenshots: the sampling service is paused, so there are no live frames to wait for
            QDir().mkpath(screenshotDir);
            QTimer::singleShot(2000, &app, [&] {
                window.resize(shotWidth, shotHeight);
                if (window.replaying()) window.replay()->seek(window.replay()->transport().count() / 2);
                for (auto page : {pmp::gui::Page::Summary, pmp::gui::Page::Performance, pmp::gui::Page::PowerFreq, pmp::gui::Page::Processes}) {
                    window.showPage(page);
                    app.processEvents();
                    window.grab().save(screenshotDir + "/replay-" + fileNameFor(page));
                }
                app.quit();
            });
        }
    }

    int frames = 0;
    if (shooting && openRecording.empty() && scanPath.empty()) {
        QDir().mkpath(screenshotDir);
        QObject::connect(svc, &pmp::gui::SamplingService::frameReady, &app, [&](pmp::gui::FrameUpdate update) {
            if (++frames != 4) return;  // let rates settle past the pending first frame
            const auto health = update.frame ? update.frame->providerHealth : std::vector<pmp::ProviderHealth>{};
            QTimer::singleShot(150, &app, [&, health, shotFrame = update.frame] {
                window.resize(shotWidth, shotHeight);
                for (int p = 0; p < pmp::gui::kPageCount; ++p) {
                    window.showPage(static_cast<pmp::gui::Page>(p));
                    app.processEvents();
                    // Pages that read the system in the background (systemctl, loginctl...) need a moment.
                    const auto page = static_cast<pmp::gui::Page>(p);
                    if (page == pmp::gui::Page::Services || page == pmp::gui::Page::StartupApps || page == pmp::gui::Page::Users) {
                        QEventLoop settle;
                        QTimer::singleShot(2000, &settle, &QEventLoop::quit);
                        settle.exec();
                    }
                    if (static_cast<pmp::gui::Page>(p) == pmp::gui::Page::Processes) {
                        // Select a process so the details pane is part of the picture.
                        if (auto* procs = dynamic_cast<pmp::gui::ProcessesPage*>(window.pageWidget(pmp::gui::Page::Processes))) {
                            for (int r = 0; r < procs->model()->rowCount(); ++r) {
                                if (procs->model()->rowAt(r).type == pmp::vm::TableRow::Type::Process) {
                                    procs->selectProcess(procs->model()->rowAt(r).key);
                                    break;
                                }
                            }
                            QEventLoop settle;
                            QTimer::singleShot(600, &settle, &QEventLoop::quit);  // the inspection runs off the UI thread
                            settle.exec();
                        }
                    }
                    window.grab().save(screenshotDir + "/" + fileNameFor(static_cast<pmp::gui::Page>(p)));
                    if (static_cast<pmp::gui::Page>(p) == pmp::gui::Page::Performance) {
                        // Also capture each subsystem's detail view.
                        auto* perf = dynamic_cast<pmp::gui::PerformancePage*>(window.pageWidget(pmp::gui::Page::Performance));
                        const char* names[] = {"cpu", "memory", "disks", "network", "gpu", "npu", "energy", "thermals"};
                        for (int c = 0; perf && c < perf->masterCards().size(); ++c) {
                            perf->masterCards().at(c)->click();
                            app.processEvents();
                            window.grab().save(screenshotDir + "/performance-" + names[static_cast<int>(perf->masterCards().at(c)->subsystem())] + ".png");
                        }
                        if (perf) perf->masterCards().at(0)->click();
                    }
                }
                // The Settings dialog, one tab per picture.
                pmp::gui::SettingsDialog dialog(window.settings(), health, &window);
                dialog.show();
                for (int t = 0; t < dialog.tabs()->count(); ++t) {
                    dialog.tabs()->setCurrentIndex(t);
                    app.processEvents();
                    dialog.grab().save(screenshotDir + "/settings-" + dialog.tabs()->tabText(t).toLower() + ".png");
                }
                if (shotFrame) {
                    pmp::gui::Screensaver saver;
                    saver.resize(shotWidth, shotHeight);
                    saver.setFrame(*shotFrame);
                    for (int i = 0; i < 40; ++i) saver.tick(0.05);  // let the levels settle and the rings spread out
                    saver.grab().save(screenshotDir + "/screensaver.png");
                }
                app.quit();
            });
        });
    }
    if (runSeconds > 0) QTimer::singleShot(runSeconds * 1000, &app, &QApplication::quit);

    const auto wallStart = std::chrono::steady_clock::now();
    const double cpuStart = cpuSeconds();
    const int rc = app.exec();
    pmp::gui::waitForAsyncWorkers(std::chrono::seconds(3));  // background reads (systemctl, ...) finish before the app object goes away
    if (runSeconds > 0) {
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
        std::printf("cpu_percent=%.2f\n", (cpuSeconds() - cpuStart) / wall * 100.0);
        if (autoRecord) std::printf("record_worst_ms=%.2f\n", window.recording()->worstAppendMs());
    }
    return rc;
}
