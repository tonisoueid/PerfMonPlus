// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/plain_text.hpp>

#include <perfmonplus/gui/clock_source.hpp>
#include <perfmonplus/gui/benchmarks_page.hpp>
#include <perfmonplus/gui/colors_popover.hpp>
#include <perfmonplus/gui/connections_page.hpp>
#include <perfmonplus/gui/disk_space_page.hpp>
#include <perfmonplus/gui/installed_apps_page.hpp>
#include <perfmonplus/gui/performance_page.hpp>
#include <perfmonplus/gui/placeholder_page.hpp>
#include <perfmonplus/gui/power_freq_page.hpp>
#include <perfmonplus/gui/processes_page.hpp>
#include <perfmonplus/gui/replay_bar.hpp>
#include <perfmonplus/gui/screensaver.hpp>
#include <perfmonplus/gui/settings_dialog.hpp>
#include <perfmonplus/gui/services_page.hpp>
#include <perfmonplus/gui/startup_page.hpp>
#include <perfmonplus/gui/summary_page.hpp>
#include <perfmonplus/gui/update_checker.hpp>
#include <perfmonplus/gui/system_info_page.hpp>
#include <perfmonplus/gui/users_page.hpp>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/linux/startup.hpp>
#include <perfmonplus/trace/player.hpp>

#include <QCloseEvent>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QPushButton>
#include <perfmonplus/gui/async.hpp>
#include <QShowEvent>
#include <algorithm>
#include <thread>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

namespace pmp::gui {
namespace {

QString hex(vm::Rgb c) { return QString("#%1%2%3").arg(c.r, 2, 16, QChar('0')).arg(c.g, 2, 16, QChar('0')).arg(c.b, 2, 16, QChar('0')); }

QString themeStyleSheet(const vm::Theme& t) {
    return QString(R"(
#root, #sidebar { background: %1; }
QWidget { color: %2; }
QLabel { background: transparent; }
#pageTitle { font-size: 24pt; }
#logo { font-size: 17pt; font-weight: bold; color: %3; padding: 14px 0; }
QListWidget { background: transparent; border: none; outline: 0; }
QListWidget::item { padding: 8px 10px; border-radius: 8px; color: %2; }
QListWidget::item:selected { background: %3; color: white; }
QListWidget::item:disabled { color: %4; font-size: 8pt; padding-top: 14px; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: %5; border-radius: 5px; min-height: 24px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolButton { background: transparent; border: none; padding: 6px 8px; color: %2; }
QToolButton:hover { background: %5; border-radius: 6px; }
#statusBar { background: %6; border-top: 1px solid %7; }
#statusBar QLabel { color: %4; }
QLineEdit, QComboBox, QPlainTextEdit { background: %6; border: 1px solid %7; border-radius: 6px; padding: 5px 8px; color: %2; selection-background-color: %3; }
QComboBox QAbstractItemView { background: %6; color: %2; selection-background-color: %3; border: 1px solid %7; }
QComboBox::drop-down { border: none; width: 18px; }
QSlider::groove:horizontal { background: %5; height: 4px; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }
QSlider::handle:horizontal { background: %3; width: 12px; margin: -5px 0; border-radius: 6px; }
QPushButton { background: %5; border: 1px solid %7; border-radius: 6px; padding: 6px 14px; color: %2; }
QPushButton:hover { background: %3; color: white; }
QPushButton:disabled { color: %4; }
QCheckBox, QRadioButton { color: %2; background: transparent; }
QDialog { background: %1; }
#updateBanner, #noticeBar { background: %3; }
#updateBanner QLabel, #noticeBar QLabel { color: white; }
QMenuBar { background: %1; color: %2; }
QMenuBar::item:selected { background: %5; }
QMenu { background: %6; color: %2; border: 1px solid %7; }
QMenu::item:selected { background: %3; color: white; }
QMenu::item:disabled { color: %4; }
#popover { background: %6; border: 1px solid %7; border-radius: 8px; }
#chip { background: %5; border-radius: 6px; padding: 6px 10px; }
QProgressBar { background: %5; border: none; border-radius: 4px; max-height: 8px; }
QProgressBar::chunk { background: %3; border-radius: 4px; }
#card { background: %6; border: 1px solid %7; border-radius: 10px; }
QTableView { background: %6; border: none; gridline-color: transparent; color: %2; }
#card[selected="true"] { border: 1px solid %3; }
QHeaderView { background: %6; }
QTabWidget::pane { background: %6; border: 1px solid %7; border-radius: 6px; }
QTabBar::tab { background: %1; color: %4; padding: 7px 14px; border: 1px solid %7; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; }
QTabBar::tab:selected { background: %6; color: %2; }
#tabPage { background: %6; }
QSpinBox { background: %1; border: 1px solid %7; border-radius: 6px; padding: 4px 8px; color: %2; }
QHeaderView::section { background: %6; color: %4; border: none; padding: 4px; }
)")
        .arg(hex(t.background), hex(t.text), hex(t.accent), hex(t.textDim), hex(t.grid), hex(t.panel), hex(t.border));
}

}  // namespace

MainWindow::MainWindow(AppSettings settings, std::unique_ptr<SamplingService> service, QWidget* parent)
    : QMainWindow(parent), settings_(std::move(settings)), history_(3600) {
    setWindowTitle("PerfMonPlus");
    setMinimumSize(900, 575);
    resize(1200, 760);

    auto* root = new QWidget(this);
    root->setObjectName("root");
    setCentralWidget(root);
    auto* row = new QHBoxLayout(root);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    sidebar_ = new Sidebar(root);
    row->addWidget(sidebar_);

    auto* right = new QWidget(root);
    auto* col = new QVBoxLayout(right);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(14, 10, 14, 6);
    auto* toggle = new QToolButton(right);
    toggle->setText("☰");
    toggle->setToolTip("Show or hide the sidebar");
    title_ = new QLabel(right);
    title_->setObjectName("pageTitle");
    header->addWidget(toggle);
    header->addWidget(title_);
    header->addStretch(1);
    col->addLayout(header);

    banner_ = new QFrame(right);
    banner_->setObjectName("updateBanner");
    auto* bannerRow = new QHBoxLayout(banner_);
    bannerRow->setContentsMargins(14, 6, 8, 6);
    bannerText_ = new QLabel(banner_);
    bannerLink_ = new QPushButton("Open download page", banner_);
    auto* bannerClose = new QToolButton(banner_);
    bannerClose->setText("✕");
    bannerClose->setToolTip("Dismiss");
    bannerRow->addWidget(bannerText_);
    bannerRow->addWidget(bannerLink_);
    bannerRow->addStretch(1);
    bannerRow->addWidget(bannerClose);
    banner_->hide();
    col->addWidget(banner_);
    notice_ = new QFrame(right);
    notice_->setObjectName("noticeBar");
    auto* noticeRow = new QHBoxLayout(notice_);
    noticeRow->setContentsMargins(14, 6, 8, 6);
    noticeText_ = new QLabel(notice_);
    noticeText_->setTextFormat(Qt::PlainText);  // file names and OS error text are not markup
    noticeText_->setWordWrap(true);
    auto* noticeClose = new QToolButton(notice_);
    noticeClose->setText("✕");
    noticeClose->setToolTip("Dismiss");
    noticeRow->addWidget(noticeText_, 1);
    noticeRow->addWidget(noticeClose);
    notice_->hide();
    col->addWidget(notice_);
    replay_ = new ReplayController(this);
    replayBar_ = new ReplayBar(replay_, right);
    replayBar_->hide();
    col->addWidget(replayBar_);
    connect(noticeClose, &QToolButton::clicked, this, &MainWindow::dismissNotice);
    connect(bannerLink_, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(bannerPage_); });
    connect(bannerClose, &QToolButton::clicked, this, &MainWindow::dismissUpdateBanner);

    stack_ = new QStackedWidget(right);
    for (int i = 0; i < kPageCount; ++i) {
        QWidget* page = nullptr;
        if (static_cast<Page>(i) == Page::Summary) {
            auto* summary = new SummaryPage(stack_);
            connect(summary, &SummaryPage::subsystemRequested, this, [this](vm::Subsystem which, int device) {
                if (auto* perf = dynamic_cast<PerformancePage*>(pages_[static_cast<std::size_t>(Page::Performance)])) {
                    perf->selectSubsystem(which, device);
                }
            });
            connect(summary, &SummaryPage::pageRequested, this, &MainWindow::showPage);
            summary->setProcessHistory(&processHistory_);
            connect(summary, &SummaryPage::processRequested, this, &MainWindow::goToProcess);
            page = summary;
        } else if (static_cast<Page>(i) == Page::Performance) {
            auto* perf = new PerformancePage(stack_);
            perf->setProcessHistory(&processHistory_);
            connect(perf, &PerformancePage::processRequested, this, &MainWindow::goToProcess);
            page = perf;
        } else if (static_cast<Page>(i) == Page::Processes) {
            auto* processes = new ProcessesPage(stack_);
            processes->applySettings(settings_);
            processes->setProcessHistory(&processHistory_);
            connect(processes, &ProcessesPage::settingsChanged, this, [this, processes] {
                processes->exportSettings(settings_);
                emit settingsChanged(settings_);
            });
            connect(processes, &ProcessesPage::commandLineCollectionRequested, this, [this](bool on) {
                if (service_) service_->setCollectCommandLines(on);
            });
            page = processes;
        } else if (static_cast<Page>(i) == Page::StartupApps) {
            page = new StartupPage(std::make_shared<linux::StartupCollector>(linux::xdgDirsFromEnvironment()), stack_);
        } else if (static_cast<Page>(i) == Page::Users) {
            auto* users = new UsersPage(std::make_shared<linux::SessionsCollector>(), stack_);
            connect(users, &UsersPage::userProcessesRequested, this, &MainWindow::showUserProcesses);
            page = users;
        } else if (static_cast<Page>(i) == Page::Services) {
            page = new ServicesPage(std::make_shared<linux::ServicesCollector>(), stack_);
        } else if (static_cast<Page>(i) == Page::Connections) {
            auto* connections = new ConnectionsPage({}, stack_);
            connections->setResolveHosts(settings_.resolveHostNames);
            connect(connections, &ConnectionsPage::processRequested, this, &MainWindow::goToProcess);
            page = connections;
        } else if (static_cast<Page>(i) == Page::InstalledApps) {
            auto collector = std::make_shared<linux::InstalledAppsCollector>(linux::AppsRoot::live(), linux::runDefault);
            auto* apps = new InstalledAppsPage(
                [collector](const std::atomic<bool>& cancel, std::vector<std::string>* skipped) { return collector->collect(cancel, skipped); },
                linux::runDefault, [] { return linux::readKernelModules(linux::ProcRoot::live()); }, stack_);
            apps->setLocator([collector](const linux::InstalledApp& a) { return collector->locate(a); });
            page = apps;
        } else if (static_cast<Page>(i) == Page::DiskSpace) {
            page = new DiskSpacePage(
                [](const linux::ScanOptions& o, const std::atomic<bool>& cancel, const std::function<void(const linux::ScanProgress&)>& progress) {
                    return linux::DiskScanner().scan(o, cancel, progress);
                },
                [] { return linux::readMounts(linux::ProcRoot::live()); }, stack_);
        } else if (static_cast<Page>(i) == Page::Benchmarks) {
            auto* bench = new BenchmarksPage([] { return makeBenchRunner(linux::BenchParams{}); }, linux::BenchStore(linux::BenchStore::defaultFile()), stack_);
            page = bench;
        } else if (static_cast<Page>(i) == Page::PowerFreq) {
            page = new PowerFreqPage(stack_);
        } else if (static_cast<Page>(i) == Page::SystemInfo) {
            page = new SystemInfoPage(stack_);
        } else {
            page = new PlaceholderPage(pageLabel(static_cast<Page>(i)), stack_);
        }
        pages_.push_back(page);
        // A page never shrinks below its natural size: a small window scrolls instead of clipping text.
        auto* scroll = new QScrollArea(stack_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        page->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        stack_->addWidget(scroll);
    }
    {  // a running benchmark pauses the other heavy features (BEN-03); a disk scan in progress blocks a benchmark
        auto* bench = dynamic_cast<BenchmarksPage*>(pages_[static_cast<std::size_t>(Page::Benchmarks)]);
        auto* conns = dynamic_cast<ConnectionsPage*>(pages_[static_cast<std::size_t>(Page::Connections)]);
        auto* disk = dynamic_cast<DiskSpacePage*>(pages_[static_cast<std::size_t>(Page::DiskSpace)]);
        if (bench && conns) connect(bench, &BenchmarksPage::runningChanged, conns, &ConnectionsPage::setPaused);
        if (bench && disk) {
            connect(bench, &BenchmarksPage::runningChanged, disk, [disk](bool on) { disk->setScanBlocked(on, "A benchmark is running."); });
            connect(disk, &DiskSpacePage::scanRunningChanged, bench, [bench](bool on) { bench->setRunBlocked(on, "A disk scan is running."); });
        }
    }
    notRecorded_ = new QLabel("Not recorded — return to live to use this page.", stack_);
    notRecorded_->setAlignment(Qt::AlignCenter);
    notRecorded_->setObjectName("dim");
    stack_->addWidget(notRecorded_);  // index kPageCount
    col->addWidget(stack_, 1);

    statusBar_ = new StatusBar(right);
    col->addWidget(statusBar_);
    row->addWidget(right, 1);

    recording_ = new RecordingController(this);
    hooks_.savePath = [this](const QString& suggested) {
        return QFileDialog::getSaveFileName(this, "Save recording", suggested, "PerfMonPlus recordings (*.pmptrace)");
    };
    hooks_.openPath = [this] {
        return QFileDialog::getOpenFileName(this, "Open recording", QString(), "PerfMonPlus recordings (*.pmptrace);;All files (*)");
    };
    hooks_.videoPath = [this](const QString& suggested) {
        return QFileDialog::getSaveFileName(this, "Save video", suggested, "Video (*.webm *.mp4)");
    };
    videoProbe_ = [] { return probeVideoSupport(); };
    video_ = new VideoRecorder(this);
    videoTick_ = new QTimer(this);
    videoTick_->setInterval(1000);  // only runs while a video is being recorded
    connect(videoTick_, &QTimer::timeout, this, [this] {
        const auto s = video_->elapsedMs() / 1000;
        statusBar_->setVideoElapsed(QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')));
    });
    connect(video_, &VideoRecorder::finished, this, [this](const QString&, bool, const QString& message) {
        videoTick_->stop();
        statusBar_->setVideoRecording(false);
        videoAction_->setText("Record &Video…");
        showNotice(message);
    });
    hooks_.unsavedChoice = [this](const QString& question) {
        QMessageBox box(QMessageBox::Question, "Unsaved recording", question, QMessageBox::NoButton, this);
        auto* save = box.addButton("Save…", QMessageBox::AcceptRole);
        auto* discard = box.addButton("Discard", QMessageBox::DestructiveRole);
        box.addButton("Cancel", QMessageBox::RejectRole);
        box.setDefaultButton(save);
        box.exec();
        if (box.clickedButton() == save) return 0;
        if (box.clickedButton() == discard) return 1;
        return 2;
    };
    connect(statusBar_, &StatusBar::recordToggled, this, &MainWindow::requestRecordToggle);
    connect(recording_, &RecordingController::progress, this, [this](const QString& tc, const QString& size) {
        statusBar_->setTimecode(tc);
        statusBar_->setSizeText(size);
        saveAction_->setEnabled(recording_->hasRecording());  // the first sample arrives after stateChanged
        saveLastAction_->setEnabled(recording_->ring() && recording_->hasRecording());
    });
    connect(recording_, &RecordingController::stateChanged, this, [this] {
        statusBar_->setRecording(recording_->recording());
        statusBar_->setRingMode(recording_->ring());
        refreshFileMenu();
    });
    connect(recording_, &RecordingController::limitReached, this, [this](int minutes) {
        showNotice(QString("Recording stopped: reached the %1 minute limit. Save it to keep it.").arg(minutes));
    });
    auto* recShortcut = new QShortcut(QKeySequence("Ctrl+R"), this);
    connect(recShortcut, &QShortcut::activated, this, &MainWindow::requestRecordToggle);
    connect(replayBar_, &ReplayBar::returnToLive, this, &MainWindow::returnToLive);
    connect(replay_, &ReplayController::positionChanged, this, [this] {
        setGraphClockOverride(replay_->monoAtPosition());
        refreshCurrentPage();
    });
    connect(replay_, &ReplayController::stateChanged, this, &MainWindow::applyReplayState);
    // Replay keys (FLR-06): only live while a recording is open, so Space and the arrows keep their usual meaning.
    auto key = [this](const QKeySequence& seq, std::function<void()> fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setEnabled(false);
        connect(sc, &QShortcut::activated, this, std::move(fn));
        replayKeys_.push_back(sc);
    };
    key(Qt::Key_Space, [this] { replay_->togglePlay(); });
    key(Qt::Key_Right, [this] { replay_->step(1); });
    key(Qt::Key_Left, [this] { replay_->step(-1); });
    key(Qt::SHIFT | Qt::Key_Right, [this] { replay_->jump(std::chrono::seconds(10)); });
    key(Qt::SHIFT | Qt::Key_Left, [this] { replay_->jump(std::chrono::seconds(-10)); });
    key(Qt::Key_Home, [this] { replay_->toStart(); });
    key(Qt::Key_End, [this] { replay_->toEnd(); });
    buildFileMenu();
    setAcceptDrops(true);

    connect(sidebar_, &Sidebar::pageSelected, this, &MainWindow::showPage);
    connect(sidebar_, &Sidebar::colorsRequested, this, &MainWindow::showColors);
    connect(sidebar_, &Sidebar::settingsRequested, this, &MainWindow::showSettings);
    connect(toggle, &QToolButton::clicked, this, [this] { setSidebarCollapsed(!sidebar_->isHidden()); });
    for (int i = 0; i < 9; ++i) {
        auto* sc = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)), this);
        connect(sc, &QShortcut::activated, this, [this, i] { showPage(static_cast<Page>(i)); });
    }

    saver_ = new Screensaver(this);
    connect(saver_, &Screensaver::closed, this, [this] { updateClock(); });
    auto* f12 = new QShortcut(QKeySequence(Qt::Key_F12), this);
    connect(f12, &QShortcut::activated, this, &MainWindow::showScreensaver);

    connect(&clock_, &AnimationClock::tick, this, [this](double dt) {
        if (saver_->isVisible()) saver_->tick(dt);
        if (auto* live = dynamic_cast<LiveView*>(pages_[static_cast<std::size_t>(current_)])) live->tick(dt);
    });
    applyAnimationRate();
    applyTheme();
    setSidebarCollapsed(settings_.sidebarCollapsed);
    showPage(static_cast<Page>(std::clamp(settings_.lastPage, 0, kPageCount - 1)));
    // Last: restoring a saved window state delivers change events, which need a fully built window.
    if (!settings_.geometry.isEmpty()) restoreGeometry(settings_.geometry);

    // One-shot facts (CPU caches, firmware, adapters...) are read off the UI thread once the window exists.
    runAsync<linux::SystemInfo>(
        this, [] { return linux::collectSystemInfo(linux::ProcRoot::live()); },
        [this](linux::SystemInfo info) {
            trace::HostMeta host;  // never a host name, serial number or address (DM-08)
            host.os = info.osName.empty() ? "Linux" : info.osName;
            host.kernel = info.kernel;
            host.cpuModel = info.cpu.model;
            host.logicalCpus = static_cast<int>(std::thread::hardware_concurrency());
            if (info.memTotal.isMeasured()) host.memoryBytes = static_cast<std::uint64_t>(*info.memTotal.value);
            recording_->setHost(std::move(host));
            for (auto* page : pages_) {
                if (auto* live = dynamic_cast<LiveView*>(page)) live->setSystemInfo(info);
            }
        });

    if (service) {
        auto* svc = service.release();
        svc->setParent(this);
        service_ = svc;
        // A Command line column restored from the settings needs the provider to collect them from the start.
        if (auto* processes = dynamic_cast<ProcessesPage*>(pages_[static_cast<std::size_t>(Page::Processes)])) {
            svc->setCollectCommandLines(processes->wantsCommandLines());
        }
        connect(svc, &SamplingService::frameReady, this, &MainWindow::applyFrame);
        svc->start();
    }
    // Opt-in update check: nothing is requested unless the user configured a manifest address.
    if (settings_.checkUpdates && !settings_.updateManifestUrl.empty()) {
        auto* checker = new UpdateChecker(this);
        connect(checker, &UpdateChecker::updateAvailable, this, &MainWindow::showUpdateBanner);
        updateCheckStarted_ = checker->check(QUrl(QString::fromStdString(settings_.updateManifestUrl)));
    }

    recording_->configure(settings_, settings_.startupInterval());
    AppSettings none;  // the baseline every runtime setting differs from, so all of them are applied once
    none.launchAtLogin = settings_.launchAtLogin;  // not a change: do not rewrite the autostart file on every start
    none.sampleIntervalMs = -1;
    none.tombstoneSeconds = settings_.tombstoneSeconds;  // the Processes page applied these when it was built
    none.showEnvironment = settings_.showEnvironment;
    none.processHistoryEnabled = settings_.processHistoryEnabled;
    none.topProcessCount = -1;
    none.historySeconds = -1;
    applyRuntimeSettings(none);
    applyProcessHistory();  // the pages were built with the live history; a saved "off" must reach them too
}

MainWindow::~MainWindow() {
    setGraphClockOverride(std::nullopt);  // a replay's clock never outlives its window
    // The recorder finishes its file while it is destroyed; its signal must not reach widgets that are already gone.
    QObject::disconnect(video_, nullptr, this, nullptr);
    delete video_;
    video_ = nullptr;
}

void MainWindow::showPage(Page p) {
    current_ = p;
    stack_->setCurrentIndex(pageIsGated(p) ? kPageCount : static_cast<int>(p));
    sidebar_->setCurrent(p);
    title_->setText(pageLabel(p));
    settings_.lastPage = static_cast<int>(p);
    refreshCurrentPage();
    updateClock();
}

// Animate only while something live is on screen (NFR-04).
void MainWindow::updateClock() {
    if (pages_.size() != static_cast<std::size_t>(kPageCount)) return;  // change events can arrive mid-construction
    const auto* live = dynamic_cast<LiveView*>(pages_[static_cast<std::size_t>(current_)]);
    const bool livePage = live && live->animates() && !pageIsGated(current_);
    clock_.setActive((isVisible() && !isMinimized() && livePage) || saver_->isVisible());
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    updateClock();
}

void MainWindow::hideEvent(QHideEvent* e) {
    QMainWindow::hideEvent(e);
    updateClock();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) updateClock();
}

QWidget* MainWindow::pageWidget(Page p) const { return pages_.at(static_cast<std::size_t>(p)); }

void MainWindow::applyFrame(const FrameUpdate& update) {
    if (!update.frame) return;
    if (replay_->active()) return;  // a frame sampled while a recording is open is not wanted
    recording_->onFrame(update);
    history_.ingest(*update.frame, update.gapBefore);
    if (settings_.processHistoryEnabled) processHistory_.ingest(*update.frame, update.gapBefore);
    statusBar_->update(*update.frame);
    latest_ = update.frame;
    if (saver_->isVisible()) saver_->setFrame(*update.frame);
    refreshCurrentPage();
}

void MainWindow::refreshCurrentPage() {
    if (pageIsGated(current_)) return;
    const bool replaying = replay_->active();
    const auto frame = replaying ? replay_->frame() : latest_;
    if (!frame) return;
    if (auto* live = dynamic_cast<LiveView*>(pages_[static_cast<std::size_t>(current_)])) {
        live->update(*frame, replaying ? replay_->history() : history_, frame->mono);
    }
}

void MainWindow::applyTheme() {
    theme_ = vm::withSaturation(vm::themeByName(settings_.themeName).value_or(vm::darkTheme()), settings_.saturation);
    setStyleSheet(themeStyleSheet(theme_));
    saver_->setTheme(theme_);
    const bool bloom = settings_.bloom && !settings_.lowEffects;
    for (auto* page : pages_) {
        if (auto* live = dynamic_cast<LiveView*>(page)) live->setTheme(theme_, bloom);
    }
    refreshCurrentPage();
}

void MainWindow::applySettings(const AppSettings& s) {
    const AppSettings previous = settings_;
    AppSettings next = s;
    next.geometry = settings_.geometry;  // window state is the window's own
    next.lastPage = settings_.lastPage;
    next.sidebarCollapsed = settings_.sidebarCollapsed;
    settings_ = next;
    applyAnimationRate();
    applyRuntimeSettings(previous);
    applyTheme();
    emit settingsChanged(settings_);
}

// Pushes the settings that are not about colours into the running app. `previous` decides what changed, so
// an unchanged value never restarts anything (the sampling interval, for one, is left alone).
void MainWindow::applyRuntimeSettings(const AppSettings& previous) {
    setFormatOptions({.binaryBytes = !settings_.decimalUnits, .fahrenheit = settings_.fahrenheit});
    for (auto* page : pages_) {
        auto* live = dynamic_cast<LiveView*>(page);
        if (!live) continue;
        if (settings_.historySeconds != previous.historySeconds) live->setHistoryWindow(settings_.historySeconds);
        if (settings_.topProcessCount != previous.topProcessCount) live->setTopProcessCount(settings_.topProcessCount);
    }
    if (service_) {
        if (settings_.sampleIntervalMs != previous.sampleIntervalMs) {
            service_->setPaused(settings_.sampleIntervalMs == 0);
            // At start-up (previous == -1) the service was already built with the saved interval; a screenshot
            // run deliberately samples faster and must keep doing so.
            if (settings_.sampleIntervalMs > 0 && previous.sampleIntervalMs != -1) {
                service_->setInterval(std::chrono::milliseconds(settings_.sampleIntervalMs));
            }
        }
        for (const QString& id : settings_.disabledProviders) {
            if (!previous.disabledProviders.contains(id)) service_->setProviderEnabled(id.toStdString(), false);
        }
        for (const QString& id : previous.disabledProviders) {
            if (!settings_.disabledProviders.contains(id)) service_->setProviderEnabled(id.toStdString(), true);
        }
    }
    if (auto* processes = dynamic_cast<ProcessesPage*>(pages_[static_cast<std::size_t>(Page::Processes)])) {
        if (settings_.tombstoneSeconds != previous.tombstoneSeconds) processes->setTombstoneSeconds(settings_.tombstoneSeconds);
        if (settings_.showEnvironment != previous.showEnvironment) processes->setShowEnvironment(settings_.showEnvironment);
    }
    if (settings_.historySeconds != previous.historySeconds) replay_->setHistoryWindow(settings_.historySeconds);
    if (auto* c = dynamic_cast<ConnectionsPage*>(pages_[static_cast<std::size_t>(Page::Connections)])) c->setResolveHosts(settings_.resolveHostNames);
    if (!settings_.processHistoryEnabled && previous.processHistoryEnabled) processHistory_.clear();
    if (settings_.processHistoryEnabled != previous.processHistoryEnabled) applyProcessHistory();
    if (recording_) recording_->configure(settings_, settings_.startupInterval());
    if (settings_.launchAtLogin != previous.launchAtLogin) {
        const std::string exec = QCoreApplication::applicationFilePath().toStdString();
        if (!linux::writeOwnAutostart(linux::xdgDirsFromEnvironment(), exec, settings_.launchAtLogin)) {
            settings_.launchAtLogin = previous.launchAtLogin;  // what is on disk is what the setting says
            showNotice("Could not change the start-at-login setting: the autostart entry could not be written.");
        }
    }
    refreshCurrentPage();
}

void MainWindow::showScreensaver() {
    if (latest_) saver_->setFrame(*latest_);
    saver_->setTheme(theme_);
    saver_->showFullScreen();
    saver_->activateWindow();
    updateClock();
}

void MainWindow::showUserProcesses(const QString& user) {
    auto* processes = dynamic_cast<ProcessesPage*>(pages_[static_cast<std::size_t>(Page::Processes)]);
    if (!processes) return;
    showPage(Page::Processes);
    processes->setView(vm::ProcessView::ByUser, false);  // a one-off view: not saved as the user's own choice
    processes->setFilter(user);
    refreshCurrentPage();
}

void MainWindow::goToProcess(ProcessKey key) {
    auto* processes = dynamic_cast<ProcessesPage*>(pages_[static_cast<std::size_t>(Page::Processes)]);
    if (!processes) return;
    showPage(Page::Processes);
    refreshCurrentPage();  // the table is only built while its page is showing
    if (!processes->selectProcess(key)) {
        processes->showMessage(processes->isRunning(key) ? "That process is hidden by the filter. Clear the filter to see it."
                                                         : "That process has exited and is no longer listed.");
    }
}

void MainWindow::showUpdateBanner(const QString& version, const QUrl& page) {
    bannerPage_ = page;
    bannerText_->setText(QString("PerfMonPlus %1 is available —").arg(version));
    banner_->show();
}

void MainWindow::dismissUpdateBanner() { banner_->hide(); }

QString MainWindow::updateBannerText() const {
    return banner_->isHidden() ? QString() : bannerText_->text() + " " + bannerLink_->text();
}

void MainWindow::showSettings() {
    SettingsDialog dialog(settings_, latest_ ? latest_->providerHealth : std::vector<ProviderHealth>{}, this);
    UpdateChecker checker;
    connect(&checker, &UpdateChecker::updateAvailable, &dialog, [&](QString v, QUrl page) {
        dialog.setUpdateStatus(QString("PerfMonPlus %1 is available.").arg(v));
        showUpdateBanner(v, page);
    });
    connect(&checker, &UpdateChecker::upToDate, &dialog, [&] { dialog.setUpdateStatus("You are up to date."); });
    connect(&checker, &UpdateChecker::failed, &dialog, [&](QString why) { dialog.setUpdateStatus("Could not check: " + why); });
    connect(&dialog, &SettingsDialog::checkNowRequested, &dialog, [&](const QString& url) {
        if (!checker.check(QUrl(url))) dialog.setUpdateStatus("Enter an https:// (or file://) address without a query string.");
        else dialog.setUpdateStatus("Checking…");
    });
    if (dialog.exec() == QDialog::Accepted) applySettings(dialog.result());
}

void MainWindow::applyAnimationRate() {
    const int hz = settings_.lowEffects ? std::min(settings_.frameRate, 30) : settings_.frameRate;
    clock_.setFrameRate(hz);
    // Low-effects mode keeps meters at up to 30 Hz but steps the graphs coarsely (~5 repaints/s):
    // that is what keeps it inside the 0.5% CPU budget with margin.
    const int graphRate = settings_.lowEffects ? std::min(hz, 15) : hz;
    for (auto* page : pages_) {
        if (auto* live = dynamic_cast<LiveView*>(page)) live->setAnimationRate(graphRate);
    }
}

void MainWindow::showColors() {
    if (!popover_) {
        popover_ = new ColorsPopover(settings_, this);
        connect(popover_, &ColorsPopover::settingsChanged, this, &MainWindow::applySettings);
    }
    popover_->setSettings(settings_);
    popover_->adjustSize();
    popover_->move(sidebar_->mapToGlobal(QPoint(8, sidebar_->height() - popover_->height() - 48)));
    popover_->show();
}

void MainWindow::setSidebarCollapsed(bool collapsed) {
    settings_.sidebarCollapsed = collapsed;
    sidebarAutoHidden_ = false;
    sidebarShownNarrow_ = !collapsed && width() < kNarrowWidth;  // an explicit "show" wins over the narrow window
    sidebar_->setVisible(!collapsed);
}

void MainWindow::adaptToWidth(int w) {
    const bool narrow = w < kNarrowWidth;
    if (narrow && !settings_.sidebarCollapsed && !sidebarShownNarrow_ && !sidebar_->isHidden()) {
        sidebar_->hide();
        sidebarAutoHidden_ = true;
    } else if (!narrow) {
        sidebarShownNarrow_ = false;
        if (sidebarAutoHidden_) {
            sidebar_->show();
            sidebarAutoHidden_ = false;
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    adaptToWidth(e->size().width());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (video_->recording()) video_->stop();
    if (!confirmDiscardRecording()) {
        e->ignore();
        return;
    }
    settings_.geometry = saveGeometry();
    emit closing(settings_);
    e->accept();
}

// ---- Flight Recorder: menu, files, notices ------------------------------------------------------------------

void MainWindow::buildFileMenu() {
    fileMenu_ = menuBar()->addMenu("&File");
    auto* open = fileMenu_->addAction("&Open Recording…");
    open->setObjectName("actionOpen");
    open->setShortcut(QKeySequence("Ctrl+O"));
    recentMenu_ = fileMenu_->addMenu("Open &Recent");
    saveAction_ = fileMenu_->addAction("&Save Recording…");
    saveAction_->setObjectName("actionSave");
    saveAction_->setShortcut(QKeySequence("Ctrl+S"));
    saveLastAction_ = fileMenu_->addAction("Save &Last Minutes…");
    saveLastAction_->setObjectName("actionSaveLast");
    fileMenu_->addSeparator();
    videoAction_ = fileMenu_->addAction("Record &Video…");
    videoAction_->setObjectName("actionVideo");
    videoAction_->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(videoAction_, &QAction::triggered, this, &MainWindow::toggleVideoRecording);
    fileMenu_->addSeparator();
    auto* quit = fileMenu_->addAction("&Quit");
    quit->setObjectName("actionQuit");
    quit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(open, &QAction::triggered, this, [this] {
        const QString path = hooks_.openPath ? hooks_.openPath() : QString();
        if (!path.isEmpty()) openRecording(path);
    });
    connect(saveAction_, &QAction::triggered, this, [this] { saveRecording(); });
    connect(saveLastAction_, &QAction::triggered, this, [this] { saveRecording(settings_.recordingRingMinutes); });
    connect(quit, &QAction::triggered, this, &QWidget::close);
    refreshFileMenu();
}

void MainWindow::refreshFileMenu() {
    saveAction_->setEnabled(recording_->hasRecording());
    saveLastAction_->setText(QString("Save Last %1 Minutes…").arg(settings_.recordingRingMinutes));
    saveLastAction_->setEnabled(recording_->ring() && recording_->hasRecording());
    recentMenu_->clear();
    for (const QString& path : settings_.recentRecordings) {
        auto* a = recentMenu_->addAction(QFileInfo(path).fileName().replace("&", "&&"));  // not a keyboard mnemonic
        a->setToolTip(plainTip(path));  // a file name is text from the disk, not markup
        connect(a, &QAction::triggered, this, [this, path] { openRecording(path); });
    }
    recentMenu_->setEnabled(!settings_.recentRecordings.isEmpty());
}

bool MainWindow::saveRecording(std::optional<int> lastMinutes) {
    if (!recording_->hasRecording()) {
        showNotice("There is nothing recorded to save.");
        return false;
    }
    const QString suggested = QString("perfmonplus-%1.pmptrace").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QString path = hooks_.savePath ? hooks_.savePath(suggested) : QString();
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += ".pmptrace";
    QString error;
    if (!recording_->save(path, &error, lastMinutes)) {
        showNotice(QString("Could not save the recording: %1").arg(error));
        return false;
    }
    settings_.addRecent(path);
    emit settingsChanged(settings_);
    refreshFileMenu();
    showNotice(QString("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::requestRecordToggle() {
    if (recording_->ring()) {  // the buffer is always recording: the button saves what just happened
        saveRecording(settings_.recordingRingMinutes);
        return;
    }
    if (!recording_->recording() && recording_->dirty() && !confirmDiscardRecording()) return;
    recording_->toggle();
}

bool MainWindow::confirmDiscardRecording() {
    if (!recording_->dirty()) return true;
    const int choice = hooks_.unsavedChoice ? hooks_.unsavedChoice("The current recording has not been saved.") : 1;
    if (choice == 0) return saveRecording() || !recording_->dirty();
    return choice == 1;
}

void MainWindow::openRecording(const QString& path) {
    if (recording_->recording() && !recording_->ring()) {
        showNotice("Stop the recording before opening another one.");
        return;
    }
    struct Opened {
        std::shared_ptr<trace::Player> player;
        trace::OpenResult result;
    };
    runAsync<Opened>(
        this,
        [path] {
            auto player = std::make_shared<trace::Player>();
            auto result = player->open(path.toStdString());
            return Opened{std::move(player), std::move(result)};
        },
        [this, path](Opened o) {
            if (!o.result.ok()) {
                showNotice(QString::fromStdString(o.result.error));
                return;
            }
            settings_.addRecent(path);
            emit settingsChanged(settings_);
            refreshFileMenu();
            replay_->setHistoryWindow(settings_.historySeconds);
            returnPage_ = current_;
            replay_->begin(o.player, path, o.result);
        });
}

bool MainWindow::pageIsGated(Page p) const {
    if (!replay_ || !replay_->active()) return false;
    return p != Page::Summary && p != Page::Performance && p != Page::PowerFreq;
}

void MainWindow::returnToLive() {
    replay_->end();  // stateChanged -> applyReplayState
}

// Which per-process history the pages read: the recording's while replaying, else the live one, or none when the
// user switched it off (the pages then say so instead of showing nothing).
void MainWindow::applyProcessHistory() {
    const ProcessHistory* h = replay_->active() ? &replay_->processHistory() : settings_.processHistoryEnabled ? &processHistory_ : nullptr;
    if (auto* pr = dynamic_cast<ProcessesPage*>(pages_[static_cast<std::size_t>(Page::Processes)])) pr->setProcessHistory(h);
    if (auto* s = dynamic_cast<SummaryPage*>(pages_[static_cast<std::size_t>(Page::Summary)])) s->setProcessHistory(h);
    if (auto* p = dynamic_cast<PerformancePage*>(pages_[static_cast<std::size_t>(Page::Performance)])) p->setProcessHistory(h);
}

// Replay started, moved or ended: show or hide the bar, gate the pages, pause or resume live sampling.
void MainWindow::applyReplayState() {
    const bool on = replay_->active();
    if (!on) setGraphClockOverride(std::nullopt);
    for (auto* k : replayKeys_) k->setEnabled(on);
    replayBar_->setVisible(on);
    statusBar_->setMessage(on ? replay_->banner() : QString());
    if (on != wasReplaying_) {
        wasReplaying_ = on;
        applyProcessHistory();
        if (service_) service_->setPaused(on || settings_.sampleIntervalMs == 0);
        if (on) {
            showPage(current_);
        } else {
            showPage(current_);
            if (latest_) statusBar_->update(*latest_);
        }
    }
    refreshCurrentPage();
    updateClock();
}

void MainWindow::toggleVideoRecording() {
    if (video_->recording()) {
        video_->stop();  // `finished` follows when ffmpeg has written the file
        videoAction_->setText("Record &Video…");
        return;
    }
    const auto support = videoProbe_ ? videoProbe_() : VideoSupport{};
    if (!support.ok) {
        showNotice(support.reason);
        return;
    }
    const QString suggested = QString("perfmonplus-%1.%2").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"), support.container);
    QString path = hooks_.videoPath ? hooks_.videoPath(suggested) : QString();
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += "." + support.container;
    QString error;
    if (!video_->start(this, path, settings_.videoFps, support, &error)) {
        showNotice(error);
        return;
    }
    statusBar_->setVideoElapsed("00:00");
    statusBar_->setVideoRecording(true);
    videoTick_->start();
    videoAction_->setText("Stop &Video Recording");
}

void MainWindow::showNotice(const QString& text) {
    noticeText_->setText(text);
    notice_->show();
}

void MainWindow::dismissNotice() { notice_->hide(); }

QString MainWindow::noticeText() const { return notice_->isHidden() ? QString() : noticeText_->text(); }

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() && e->mimeData()->urls().size() == 1 && e->mimeData()->urls().first().isLocalFile()) {
        e->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls() || e->mimeData()->urls().size() != 1) return;
    openRecording(e->mimeData()->urls().first().toLocalFile());
    e->acceptProposedAction();
}

}  // namespace pmp::gui
