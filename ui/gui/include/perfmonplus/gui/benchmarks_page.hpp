// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/linux/bench_kernels.hpp>
#include <perfmonplus/linux/bench_store.hpp>
#include <perfmonplus/vm/bench_model.hpp>

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace pmp::gui {

// What the page needs from the benchmark engine (so tests can run a fake one).
class BenchRunner {
public:
    virtual ~BenchRunner() = default;
    virtual std::string preflight(linux::BenchId id) = 0;
    virtual std::uint64_t estimate(linux::BenchId id) = 0;
    virtual linux::BenchResult run(linux::BenchId id, const std::atomic<bool>& cancel, const std::function<void(double)>& progress) = 0;
    virtual void cleanup() = 0;
    virtual std::string tempDir() = 0;
    virtual std::uint64_t diskBytes() = 0;
};

std::unique_ptr<BenchRunner> makeBenchRunner(const linux::BenchParams& params);

// Benchmarks (§7.13): short, reproducible tests with a warning first, live progress and result cards. The sampler
// keeps running, so the Performance page shows the load while a test runs. Results stay on this machine.
class BenchmarksPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    using RunnerFactory = std::function<std::unique_ptr<BenchRunner>()>;
    struct Hooks {
        std::function<bool(const QString& title, const QString& text)> confirmStart;  // default: a box whose default is Cancel
    };

    BenchmarksPage(RunnerFactory factory, linux::BenchStore store, QWidget* parent = nullptr);
    ~BenchmarksPage() override;

    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }
    void setSystemInfo(const linux::SystemInfo& info) override;
    void setHooks(Hooks h);
    void setRunBlocked(bool blocked, const QString& reason);  // a disk scan in progress blocks a run

    void start();   // asks first (BEN-03), then runs the ticked tests one after another
    void cancel();
    bool running() const { return running_; }
    QString warningText() const { return lastWarning_; }
    QCheckBox* checkFor(linux::BenchId id) const { return checks_[static_cast<std::size_t>(id)]; }
    QCheckBox* gpuCheck() const { return gpu_; }
    QPushButton* startButton() const { return start_; }
    QPushButton* cancelButton() const { return cancel_; }
    QProgressBar* progressBar() const { return bar_; }
    QString statusText() const { return status_->text(); }
    QString currentText() const { return current_->text(); }
    QString noticeText() const { return notice_->text(); }
    QListWidget* historyList() const { return history_; }
    const std::vector<vm::ResultCard>& cards() const { return cards_; }
    QFrame* cardFrame(int i) const;
    QString storeWarning() const { return storeWarning_; }

signals:
    void runningChanged(bool running);
    void runFinished();

private:
    void finish(std::vector<linux::BenchResult> results, bool cancelled);
    void showCards(const linux::BenchRecord& latest);
    void reloadHistory();
    void updateButtons();
    QString warning(const std::vector<linux::BenchId>& ids, BenchRunner& runner) const;

    RunnerFactory factory_;
    linux::BenchStore store_;
    Hooks hooks_;
    std::unique_ptr<BenchRunner> runner_;
    std::thread worker_;
    std::atomic<bool> cancelFlag_{false};
    bool running_ = false;
    bool blocked_ = false;
    QString blockReason_;
    PowerSource source_ = PowerSource::Unknown;
    linux::BenchRecord context_;  // CPU model, kernel, cores: filled from the system info
    QString lastWarning_, storeWarning_;
    std::vector<QCheckBox*> checks_;
    QCheckBox* gpu_;
    QPushButton *start_, *cancel_;
    QProgressBar* bar_;
    QLabel *status_, *current_, *notice_;
    QGridLayout* cardGrid_;
    QWidget* cardHost_;
    QListWidget* history_;
    std::vector<vm::ResultCard> cards_;
    std::vector<linux::BenchRecord> records_;
};

}  // namespace pmp::gui
