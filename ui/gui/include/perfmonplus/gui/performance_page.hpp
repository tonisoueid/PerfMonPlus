// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/gui/culprits_popover.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/vm/hardware_model.hpp>
#include <perfmonplus/vm/performance_model.hpp>

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>
#include <QWidget>

#include <vector>

namespace pmp::gui {

// A clickable card in the Performance master list: title, two lines, sparkline.
class MasterCard : public QFrame {
    Q_OBJECT
public:
    MasterCard(vm::Subsystem which, int device, QWidget* parent = nullptr);
    void set(const vm::MasterEntry& entry);
    void setSelected(bool selected);
    HistoryGraph* spark() const { return spark_; }
    void click() { emit clicked(); }
    vm::Subsystem subsystem() const { return which_; }
    int device() const { return device_; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent*) override { emit clicked(); }

private:
    vm::Subsystem which_;
    int device_;
    QLabel* title_;
    QLabel* line1_;
    QLabel* line2_;
    HistoryGraph* spark_;
};

// One mini graph per logical processor (§7.2.1), wrapped by vm::gridLayout.
class CpuGrid : public QWidget {
public:
    explicit CpuGrid(QWidget* parent = nullptr);
    void update(const Frame& f, const HistoryStore& history, MonoTime now);
    void setTheme(const vm::Theme& theme, bool bloom);
    const std::vector<HistoryGraph*>& cells() const { return cells_; }

protected:
    void resizeEvent(QResizeEvent*) override { relayout(); }

private:
    void relayout();
    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    std::vector<HistoryGraph*> cells_;
};

// One mini graph per temperature sensor (§7.2.8), wrapped by vm::gridLayout.
class SensorGrid : public QWidget {
public:
    explicit SensorGrid(QWidget* parent = nullptr);
    void update(const Frame& f, const HistoryStore& history, MonoTime now);
    void setTheme(const vm::Theme& theme, bool bloom);
    const std::vector<HistoryGraph*>& cells() const { return cells_; }

protected:
    void resizeEvent(QResizeEvent*) override { relayout(); }

private:
    void relayout();
    vm::Theme theme_ = vm::darkTheme();
    std::vector<HistoryGraph*> cells_;
};

class DetailPanel : public QWidget {
public:
    DetailPanel(vm::Subsystem which, int device, QWidget* parent = nullptr);
    void update(const Frame& f, const HistoryStore& history, MonoTime now, const vm::MasterEntry& entry);
    void setTheme(const vm::Theme& theme, bool bloom);
    void tick(double dtSeconds);
    void setAnimationRate(int hz);
    HistoryGraph* mainGraph() const { return graphs_.empty() ? nullptr : graphs_[0]; }
    QString title() const { return title_->text(); }
    QString statValue(const QString& caption) const;
    CpuGrid* cpuGrid() const { return cpuGrid_; }
    void setCpuFacts(std::vector<vm::StatItem> facts) { cpuFacts_ = std::move(facts); }
    SensorGrid* sensorGrid() const { return sensorGrid_; }
    std::function<void(ProcessKey)> processClicked;  // a row of "processes with the highest energy demand" was clicked

private:
    void setStats(const std::vector<vm::StatItem>& items);
    bool eventFilter(QObject* watched, QEvent* event) override;

    vm::Subsystem which_;
    int device_;
    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    QLabel* title_;
    QLabel* subtitle_;
    QLabel* value_;
    SegmentBar* bar_;
    GraphCursorHub* cursorHub_ = new GraphCursorHub(this);
    std::vector<HistoryGraph*> graphs_;
    std::vector<QLabel*> graphCaptions_;
    CpuGrid* cpuGrid_ = nullptr;
    QLabel* partialNote_ = nullptr;  // GPU: "other users' work is not counted"
    // Energy
    std::vector<QLabel*> chips_;
    struct DemandRow {
        QLabel* name;
        QProgressBar* bar;
        QLabel* value;
    };
    std::vector<DemandRow> demand_;
    std::vector<ProcessKey> demandKeys_;
    QLabel* footnote_ = nullptr;
    QLabel* restrictedHelp_ = nullptr;
    // Thermals
    SensorGrid* sensorGrid_ = nullptr;
    QLabel* noSensors_ = nullptr;
    std::vector<vm::StatItem> cpuFacts_;  // static CPU facts appended to the CPU stats
    QGridLayout* statsLayout_;
    std::vector<StatBlock*> stats_;
};

class PerformancePage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit PerformancePage(QWidget* parent = nullptr);

    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme& theme, bool bloom) override;
    void tick(double dtSeconds) override;
    void setAnimationRate(int hz) override;
    void setHistoryWindow(int seconds) override;
    void setSystemInfo(const linux::SystemInfo& info) override;
    void setProcessHistory(const ProcessHistory* history) { culprits_->setHistory(history); }
    CulpritsPresenter* culprits() const { return culprits_; }

    void selectSubsystem(vm::Subsystem which, int device = 0);
    QList<MasterCard*> masterCards() const { return cards_; }
    QString detailTitle() const;
    const std::vector<HistoryGraph*>& cpuCells() const { return panels_[0]->cpuGrid()->cells(); }  // CPU is always first
    int cpuGridWidth() const { return panels_[0]->cpuGrid()->width(); }
    const std::vector<HistoryGraph*>& sensorTiles() const;  // the Thermals panel's tiles (empty if absent)
    QString statValue(const QString& caption) const;

signals:
    void processRequested(pmp::ProcessKey key);  // "Go to process" in the culprits popover

private:
    CulpritsPresenter* culprits_ = new CulpritsPresenter(this);
    struct Key {
        vm::Subsystem which;
        int device;
        friend bool operator==(const Key&, const Key&) = default;
    };
    void select(int index);
    void refresh();
    void syncPanels(const std::vector<vm::MasterEntry>& entries);
    int selectedIndex() const;

    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    QList<MasterCard*> cards_;
    std::vector<DetailPanel*> panels_;
    std::vector<Key> keys_;
    Key selectedKey_{vm::Subsystem::Cpu, 0};
    QVBoxLayout* leftLayout_ = nullptr;
    QStackedWidget* stack_;
    Frame last_;
    std::vector<vm::MasterEntry> entries_;
    const HistoryStore* history_ = nullptr;
    MonoTime now_{};
};

}  // namespace pmp::gui
