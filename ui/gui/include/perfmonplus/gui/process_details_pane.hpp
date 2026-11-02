// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/gui/meter_widgets.hpp>
#include <perfmonplus/linux/process_inspector.hpp>
#include <perfmonplus/vm/process_details.hpp>
#include <perfmonplus/vm/theme.hpp>

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

#include <array>
#include <memory>
#include <optional>

namespace pmp::gui {

// The pane under the process table (PRC-11/12): "{Name} ({PID})", the executable path, five columns of facts, a
// mini CPU and memory graph, and — only when asked for — the command line and the environment. Slow or private
// facts come from a ProcessInspector run off the UI thread when the selection changes and then every 5 s, never
// per frame. Every string from the process is shown as plain text.
class ProcessDetailsPane : public QFrame {
    Q_OBJECT
public:
    explicit ProcessDetailsPane(std::shared_ptr<const linux::ProcessInspector> inspector, QWidget* parent = nullptr);

    void setProcess(const ProcessRecord* record, const ProcessRecord* parent);   // nullptr: nothing selected
    void update(const Frame& frame, const ProcessHistory* history);             // refreshes numbers and graphs
    void setShowEnvironment(bool on);
    void setTheme(const vm::Theme& theme, bool bloom);

    QString titleText() const { return title_->text(); }
    QString pathText() const { return path_->text(); }
    QString statusText() const { return status_->text(); }
    QString openFilesText() const { return openFiles_->text(); }
    QString commandLineText() const { return commandVisible_ ? cmdline_->toPlainText() : QString(); }   // "" until Show is clicked
    QString environmentText() const { return env_->isVisible() ? env_->toPlainText() : QString(); }
    QString valueOf(const QString& section, const QString& caption) const;
    QPushButton* showCommandLineButton() const { return showCmdline_; }
    HistoryGraph* cpuGraph() const { return cpuGraph_; }
    HistoryGraph* memoryGraph() const { return memGraph_; }
    int inspectionCount() const { return inspections_; }
    bool timerActive() const { return timer_.isActive(); }  // never ticking while the pane or its page is hidden (NFR-04)

signals:
    void inspected();  // an inspection finished (or found the process gone)

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    struct Column {
        QLabel* header = nullptr;
        std::array<QLabel*, 4> caption{}, value{};
    };
    void inspect();
    void render();
    void showInspection();

    std::shared_ptr<const linux::ProcessInspector> inspector_;
    std::optional<ProcessRecord> record_;
    std::optional<ProcessRecord> parent_;
    std::optional<linux::Inspection> inspection_;
    bool exited_ = false;
    bool commandVisible_ = false;
    bool showEnvironment_ = false;
    int inspections_ = 0;
    std::uint64_t request_ = 0;  // answers to an older selection are dropped
    vm::CellContext ctx_;
    std::optional<HistoryRing<double>> cpuRing_, memRing_;

    QLabel *title_, *path_, *status_, *openFiles_, *exitNote_;
    std::array<Column, 5> columns_;
    QPushButton* showCmdline_;
    QPlainTextEdit *cmdline_, *env_;
    HistoryGraph *cpuGraph_, *memGraph_;
    QTimer timer_;
};

}  // namespace pmp::gui
