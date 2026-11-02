// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/gui/culprits_popover.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/gui/pages.hpp>
#include <perfmonplus/vm/hardware_model.hpp>
#include <perfmonplus/vm/performance_model.hpp>
#include <perfmonplus/vm/summary_model.hpp>

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include <array>

namespace pmp::gui {

// A clickable Summary tile (VIS-10): title, value, subtitle and a segmented meter.
class TileCard : public QFrame {
    Q_OBJECT
public:
    TileCard(const QString& title, vm::MeterKind kind, QWidget* parent = nullptr);
    void set(const vm::TileText& text, const Metric& fill, double fullScale);
    void setTheme(const vm::Theme& theme, bool bloom);
    void tick(double dtSeconds) { bar_->tick(dtSeconds); }
    void click() { emit clicked(); }
    QString valueText() const { return value_->text(); }
    const QString& title() const { return title_; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent*) override { emit clicked(); }

private:
    QString title_;
    QLabel* value_;
    QLabel* subtitle_;
    SegmentBar* bar_;
};

class SummaryPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit SummaryPage(QWidget* parent = nullptr);

    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme& theme, bool bloom) override;
    void tick(double dtSeconds) override;
    void setSystemInfo(const linux::SystemInfo& info) override {
        if (info.cpu.maxHz.isMeasured()) clockFullScale_ = *info.cpu.maxHz.value;
    }
    void setProcessHistory(const ProcessHistory* history) { culprits_->setHistory(history); }
    CulpritsPresenter* culprits() const { return culprits_; }
    void setAnimationRate(int hz) override;
    void setHistoryWindow(int seconds) override;
    void setTopProcessCount(int count) override;

    QString cpuValueText() const { return cpuValue_->text(); }
    QTableWidget* processTable() const { return table_; }
    HistoryGraph* cpuGraph() const { return cpuGraph_; }
    SegmentBar* cpuMeter() const { return vfdBars_[0]; }
    QAbstractButton* legendButton(int i) const { return legend_.at(static_cast<std::size_t>(i)); }
    QList<TileCard*> tiles() const {
        QList<TileCard*> out;
        for (const auto& t : tiles_) out.push_back(t.card);
        return out;
    }
    void setClockFullScale(double hz) { clockFullScale_ = hz; }  // the CPU's maximum frequency, once known

signals:
    void pageRequested(pmp::gui::Page page);
    void processRequested(pmp::ProcessKey key);  // "Go to process" in the culprits popover
    void subsystemRequested(pmp::vm::Subsystem which, int device);  // which Performance panel (and which GPU/NPU) the clicked tile belongs to

private:
    void applyCpuSeries();

    CulpritsPresenter* culprits_ = new CulpritsPresenter(this);
    std::vector<ProcessKey> topKeys_;  // the process behind each row of the table
    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    const HistoryStore* history_ = nullptr;
    MonoTime now_{};
    int windowSeconds_ = 60;
    int topRows_ = 12;
    QLabel* procTitle_ = nullptr;

    QFrame* vfdCard_ = nullptr;
    std::array<QWidget*, 4> vfdColumns_{};
    std::array<SegmentBar*, 4> vfdBars_{};
    std::array<QLabel*, 4> vfdValues_{};
    QLabel* cpuValue_;
    QLabel* cpuFooterLeft_;
    QLabel* cpuFooterRight_;
    std::array<QPushButton*, 3> legend_{};
    HistoryGraph* cpuGraph_;
    QLabel* totalProcesses_;
    QTableWidget* table_;
    SegmentBar* memBar_;
    QLabel* memPct_;
    QLabel* memHeader_;
    HistoryGraph* memGraph_;
    QLabel* memAvailable_;
    QLabel* memCached_;
    QLabel* memSwap_;
    TileCard* diskTile_;
    TileCard* netTile_;

    struct TileEntry {
        std::string key;
        TileCard* card;
        vm::Subsystem which;
        int device;
    };
    void syncTiles(const Frame& f);
    void connectTile(TileCard* card, vm::Subsystem which, int device);
    QGridLayout* tileGrid_ = nullptr;
    std::vector<TileEntry> tiles_;
    double clockFullScale_ = 5e9;
};

}  // namespace pmp::gui
