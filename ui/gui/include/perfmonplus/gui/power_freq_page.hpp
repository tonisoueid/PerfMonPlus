// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/vm/power_freq_model.hpp>

#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QWidget>

#include <chrono>
#include <vector>

namespace pmp::gui {

// Power & Freq (§7.9): per-core frequency graphs coloured by core class, power-domain graphs, throttling and
// governor facts, and the battery health card.
class PowerFreqPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit PowerFreqPage(QWidget* parent = nullptr);

    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme& theme, bool bloom) override;
    void setHistoryWindow(int seconds) override;
    bool animates() const override { return false; }  // graphs step once per sample

    const std::vector<HistoryGraph*>& frequencyGraphs() const { return freqCells_; }
    const std::vector<HistoryGraph*>& powerGraphs() const { return powerCells_; }
    QString governorText() const { return governor_->value(); }
    QString throttleText() const { return throttle_->value(); }
    QString helpText() const { return help_->text(); }
    QString batteryText() const { return battery_->text(); }

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    struct Grid {
        QWidget* host = nullptr;
    };
    void relayout();
    void syncCells(std::vector<HistoryGraph*>& cells, QWidget* host, std::size_t n);

    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    std::chrono::seconds window_{60};
    QWidget* freqHost_;
    QWidget* powerHost_;
    std::vector<HistoryGraph*> freqCells_, powerCells_;
    StatBlock *governor_, *epp_, *driver_, *throttle_;
    QLabel* powerNote_;
    QLabel* help_;
    QLabel* battery_;
    QLabel* batteryTitle_;
};

}  // namespace pmp::gui
