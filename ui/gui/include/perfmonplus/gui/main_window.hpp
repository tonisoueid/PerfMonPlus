// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/gui/animation_clock.hpp>
#include <perfmonplus/gui/app_settings.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/gui/recording_controller.hpp>
#include <perfmonplus/gui/replay_bar.hpp>
#include <perfmonplus/gui/replay_controller.hpp>
#include <perfmonplus/gui/video_recorder.hpp>
#include <perfmonplus/gui/sampling_service.hpp>
#include <perfmonplus/gui/sidebar.hpp>
#include <perfmonplus/gui/status_bar.hpp>
#include <perfmonplus/vm/theme.hpp>

#include <QFrame>
#include <QLabel>
#include <QUrl>
#include <QMainWindow>
#include <QTimer>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pmp::gui {

// The file dialogs and the "unsaved recording" question, replaceable so tests can answer them.
struct FileHooks {
    std::function<QString(const QString& suggestedName)> savePath;  // "" = cancelled
    std::function<QString()> openPath;                              // "" = cancelled
    std::function<int(const QString& question)> unsavedChoice;      // 0 = Save, 1 = Discard, 2 = Cancel
    std::function<QString(const QString& suggestedName)> videoPath;  // "" = cancelled
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppSettings settings = {}, std::unique_ptr<SamplingService> service = nullptr,
                        QWidget* parent = nullptr);

    ~MainWindow() override;

    Page currentPage() const { return current_; }
    void showPage(Page p);
    QWidget* pageWidget(Page p) const;
    QStringList sidebarLabels() const { return sidebar_->labels(); }
    // Below kNarrowWidth the sidebar steps aside so the pages are not cut off; the user's own choice is kept and
    // comes back when the window is wide again. `adaptToWidth` is what a resize calls.
    static constexpr int kNarrowWidth = 1060;
    void adaptToWidth(int width);
    bool sidebarHidden() const { return sidebar_->isHidden(); }
    QString statusText() const { return statusBar_->text(); }

    // Also driven by the sampling service; public so tests can feed frames directly.
    void applyFrame(const FrameUpdate& update);
    HistoryStore& history() { return history_; }
    const ProcessHistory& processHistory() const { return processHistory_; }
    AppSettings settings() const { return settings_; }

    bool animationActive() const { return clock_.isActive(); }
    int animationIntervalMs() const { return clock_.intervalMs(); }

    // Update check (opt-in): true once a request for a configured manifest address was sent.
    bool updateCheckStarted() const { return updateCheckStarted_; }
    void showUserProcesses(const QString& user);  // the Processes page, by user, filtered to them
    void goToProcess(ProcessKey key);   // opens the Processes page on that process
    void showUpdateBanner(const QString& version, const QUrl& page);
    void dismissUpdateBanner();
    QString updateBannerText() const;

    // Flight Recorder (§8.2): the footer button, the File menu and the shortcuts all end up here.
    RecordingController* recording() const { return recording_; }
    void setFileHooks(FileHooks hooks) { hooks_ = std::move(hooks); }
    bool saveRecording(std::optional<int> lastMinutes = {});  // asks for a file name; false when cancelled or failed
    void openRecording(const QString& path);
    void requestRecordToggle();
    // Replay (§8.2): opens a trace and shows it through the live pages. Reading happens off the UI thread.
    // Screen video recording (§8.3): records this window through ffmpeg; says what is missing when it cannot.
    void toggleVideoRecording();
    bool videoRecording() const { return video_->recording(); }
    void setVideoSupportProbe(std::function<VideoSupport()> probe) { videoProbe_ = std::move(probe); }
    QAction* videoAction() const { return videoAction_; }
    ReplayController* replay() const { return replay_; }
    ReplayBar* replayBar() const { return replayBar_; }
    bool replaying() const { return replay_->active(); }
    void returnToLive();
    QWidget* notRecordedPage() const { return notRecorded_; }
    bool pageIsGated(Page p) const;  // true while replaying and `p` has nothing recorded to show  // the footer button / Ctrl+R
    QString noticeText() const;
    void showNotice(const QString& text);
    void dismissNotice();
    QMenu* fileMenu() const { return fileMenu_; }
    QMenu* recentMenu() const { return recentMenu_; }
    QAction* saveAction() const { return saveAction_; }
    QAction* saveLastAction() const { return saveLastAction_; }
    StatusBar* footer() const { return statusBar_; }

    void applyTheme();  // rebuilds the stylesheet and pushes the theme to every live page

    // Takes every user setting of `s` (everything but window geometry, last page and sidebar state), applies it
    // live and reports it through settingsChanged so the app can persist it.
    void applySettings(const AppSettings& s);

signals:
    void settingsChanged(pmp::gui::AppSettings settings);
    // Emitted when the window closes. The app decides what to persist, so one-run command-line
    // overrides (--low-effects, --theme, screenshots) never leak into the saved settings.
    void closing(pmp::gui::AppSettings settings);

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void refreshCurrentPage();
    void setSidebarCollapsed(bool collapsed);
    void showColors();
    void updateClock();
    void applyAnimationRate();
    void applyRuntimeSettings(const AppSettings& previous);
    void showSettings();
    void showScreensaver();
    void buildFileMenu();
    void refreshFileMenu();
    void applyReplayState();
    void applyProcessHistory();
    bool confirmDiscardRecording();  // false when the user cancels

    AppSettings settings_;
    vm::Theme theme_;
    HistoryStore history_;
    ProcessHistory processHistory_;
    Sidebar* sidebar_ = nullptr;
    bool sidebarAutoHidden_ = false;   // hidden only because the window is narrow; not saved
    bool sidebarShownNarrow_ = false;  // the user opened it despite the narrow window
    QStackedWidget* stack_ = nullptr;
    QLabel* title_ = nullptr;
    StatusBar* statusBar_ = nullptr;
    std::vector<QWidget*> pages_;
    std::shared_ptr<const Frame> latest_;
    Page current_ = Page::Summary;
    class ColorsPopover* popover_ = nullptr;
    SamplingService* service_ = nullptr;
    class Screensaver* saver_ = nullptr;
    QFrame* banner_ = nullptr;
    QLabel* bannerText_ = nullptr;
    QPushButton* bannerLink_ = nullptr;
    QUrl bannerPage_;
    bool updateCheckStarted_ = false;
    AnimationClock clock_;
    RecordingController* recording_ = nullptr;
    FileHooks hooks_;
    VideoRecorder* video_ = nullptr;
    QTimer* videoTick_ = nullptr;
    QAction* videoAction_ = nullptr;
    std::function<VideoSupport()> videoProbe_;
    ReplayController* replay_ = nullptr;
    ReplayBar* replayBar_ = nullptr;
    QLabel* notRecorded_ = nullptr;
    std::vector<QShortcut*> replayKeys_;
    bool wasReplaying_ = false;
    Page returnPage_ = Page::Summary;
    QFrame* notice_ = nullptr;
    QLabel* noticeText_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveLastAction_ = nullptr;
};

}  // namespace pmp::gui
