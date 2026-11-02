// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/gui/app_settings.hpp>
#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/core/process_history.hpp>
#include <perfmonplus/gui/process_actions_controller.hpp>
#include <perfmonplus/gui/process_details_pane.hpp>
#include <perfmonplus/gui/process_table_model.hpp>
#include <perfmonplus/linux/process_inspector.hpp>
#include <perfmonplus/vm/process_table.hpp>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QSplitter>
#include <QTableView>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <set>

namespace pmp::gui {

class ProcessTableView;

// The Processes page (§7.3): a view-mode combo and a filter box over a table of processes, with inline heat
// bars, expandable groups and trees, and red tombstone rows for processes that just exited.
class ProcessesPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    explicit ProcessesPage(QWidget* parent = nullptr);
    explicit ProcessesPage(std::shared_ptr<const linux::ProcessInspector> inspector, QWidget* parent = nullptr);

    // The table is rebuilt only while the page is visible (NFR-04).
    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme& theme, bool bloom) override;
    bool animates() const override { return false; }

    void applySettings(const AppSettings& s);   // view, sort, columns, tombstone retention; emits nothing
    void exportSettings(AppSettings& s) const;

    QTableView* table() const;
    ProcessTableModel* model() const { return model_; }
    QComboBox* viewCombo() const { return view_; }
    QLineEdit* filterField() const { return filter_; }
    std::optional<ProcessKey> selectedKey() const;
    int rebuildCount() const { return rebuilds_; }
    std::vector<vm::ProcessColumn> columns() const { return columns_; }
    bool wantsCommandLines() const { return std::find(columns_.begin(), columns_.end(), vm::ProcessColumn::CommandLine) != columns_.end(); }

    void setFilter(const QString& text);
    // `remember` false: a one-off view (e.g. "this user's processes") that is not saved as the user's own choice.
    void setView(vm::ProcessView v, bool remember = true);
    void setTombstoneSeconds(int seconds);
    void setColumnVisible(vm::ProcessColumn column, bool visible);  // Name cannot be hidden
    bool isRunning(ProcessKey key) const;  // in the latest frame and not exited, whatever the filter hides
    bool selectProcess(ProcessKey key);                             // opens whatever hides it, selects it, scrolls to it
    void toggleExpanded(int row);
    void setProcessHistory(const ProcessHistory* history) { history_ = history; }
    void setShowEnvironment(bool on);
    void setDetailsOpen(bool open);
    bool detailsOpen() const { return wantDetails_; }  // the user's choice; the pane shows only while a row is selected
    ProcessDetailsPane* details() const { return details_; }
    ProcessActionsController* actions() const { return actions_; }
    void setActionPort(std::shared_ptr<ProcessActionPort> port) { actions_->setPort(std::move(port)); }
    void pinTombstone(ProcessKey key);
    void unpinTombstone(ProcessKey key);
    QString messageText() const { return message_->isHidden() ? QString() : message_->text(); }
    QPushButton* forceQuitButton() const { return forceBtn_; }
protected:
    void hideEvent(QHideEvent* e) override;

public:
    bool forceQuitBarVisible() const { return !forceBar_->isHidden(); }
    void endSelectedTask(bool viaKeyboard);   // what Delete does
    void showMessage(const QString& text);    // plain text in the message bar for a few seconds
    void sortBy(vm::ProcessColumn column);                          // what clicking the header does

signals:
    void settingsChanged();                          // view, sort or columns changed: the owner persists them
    void commandLineCollectionRequested(bool on);    // the Command line column was shown or hidden

private:
    friend class ProcessTableView;
    void rebuild();
    void applyColumns();
    void applySpans();
    void updateSortIndicator();
    void onSelectionChanged();
    void showContextMenu(const QPoint& at);
    void updateActionState();
    std::optional<ProcessRecord> selectedRecord() const;
    std::vector<ProcessRecord> descendantsLeavesFirst(ProcessKey key) const;
    const ProcessRecord* find(ProcessKey key) const;
    void showColumnMenu(const QPoint& at);
    vm::ProcessTableOptions options() const;

    ProcessTableModel* model_;
    ProcessTableView* table_;
    QComboBox* view_;
    QLineEdit* filter_;
    QLabel* message_;
    QSplitter* split_;
    ProcessDetailsPane* details_;
    ProcessActionsController* actions_;
    QFrame* forceBar_;
    QLabel* forceLabel_;
    QPushButton* forceBtn_;
    QToolButton *endBtn_, *suspendBtn_, *resumeBtn_;
    QTimer messageTimer_;
    ProcessKey forceKey_;
    std::vector<ProcessRecord> pinned_;
    const ProcessHistory* history_ = nullptr;
    Frame frame_;
    bool haveFrame_ = false;
    vm::ProcessView mode_ = vm::ProcessView::Grouped;
    vm::ProcessColumn sortBy_ = vm::ProcessColumn::Memory;
    bool descending_ = true;
    std::vector<vm::ProcessColumn> columns_;
    std::set<std::string> toggled_;
    int tombstoneSeconds_ = 10;
    std::optional<ProcessKey> reveal_;
    int rebuilds_ = 0;
    bool ignoreActivate_ = false;
    bool rememberView_ = true;
    int savedViewIndex_ = 0;  // the view the user chose; a one-off view does not replace it
    bool wantDetails_ = true;  // the user's wish; the pane is only shown while something is selected
};

}  // namespace pmp::gui
