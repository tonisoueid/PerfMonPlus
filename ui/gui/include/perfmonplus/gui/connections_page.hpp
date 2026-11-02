// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/linux/connections.hpp>
#include <perfmonplus/vm/connections_model.hpp>

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <functional>
#include <map>
#include <memory>

namespace pmp::gui {

class ConnectionsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ConnectionsModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    void setRows(std::vector<vm::ConnectionRow> rows);
    const vm::ConnectionRow& rowAt(int i) const { return rows_.at(static_cast<std::size_t>(i)); }
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& = {}) const override { return 8; }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    static const char* sortKey(int column);

private:
    std::vector<vm::ConnectionRow> rows_;
};

// Connections (§7.10): who is talking to what, and which ports this computer exposes. Everything from other programs
// (process names, host names) is plain text. Polls every two seconds, and only while the page is visible.
class ConnectionsPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    using CollectFn = std::function<linux::ConnectionList()>;
    using ResolveFn = std::function<std::string(const linux::IpAddr&)>;  // "" when the name cannot be found

    explicit ConnectionsPage(CollectFn collect = {}, QWidget* parent = nullptr);

    void update(const Frame& frame, const HistoryStore& history, MonoTime now) override;
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }

    void refresh();      // collects off the UI thread
    void refreshSync();  // collects on the calling thread (tests, screenshots)
    void setResolveHosts(bool on);
    void setResolver(ResolveFn fn) { resolve_ = std::move(fn); }
    // A name found is trusted for 10 minutes, a failure for 5: addresses change hands, and a lookup that failed once may not fail again.
    void setClock(std::function<std::chrono::steady_clock::time_point()> now) { now_ = std::move(now); }
    static constexpr std::chrono::minutes kResolvedFor{10}, kFailedFor{5};
    void setPaused(bool paused);  // a running benchmark pauses the polling (BEN-03)
    bool timerActive() const { return timer_.isActive(); }

    QTableView* table() const { return table_; }
    ConnectionsModel* model() const { return model_; }
    QLineEdit* filterEdit() const { return filter_; }
    QCheckBox* listeningBox() const { return listening_; }
    QComboBox* protoBox() const { return proto_; }
    QComboBox* stateBox() const { return state_; }
    QPushButton* goButton() const { return go_; }
    QPushButton* copyButton() const { return copy_; }
    QString statusText() const { return status_->text(); }
    QString noteText() const { return note_->isHidden() ? QString() : note_->text(); }
    QString copyText() const;
    void selectKey(const QString& key);
    QString selectedKey() const;
    bool canGoToProcess() const;
    void goToProcess();

signals:
    void processRequested(pmp::ProcessKey key);

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void apply(linux::ConnectionList list);
    std::size_t namesSignature() const;  // what the process names the rows show would be: a change means a rebuild
    void rebuild();
    void startLookups();
    vm::ConnectionOptions options() const;
    void updateButtons();

    CollectFn collect_;
    ResolveFn resolve_;
    bool resolveHosts_ = false;
    bool paused_ = false;
    bool inFlight_ = false;
    linux::ConnectionList list_;
    Frame processes_;  // only the process table is used
    vm::HostLookups hosts_;
    std::map<linux::IpAddr, std::chrono::steady_clock::time_point> lookedUpAt_;
    std::function<std::chrono::steady_clock::time_point()> now_ = [] { return std::chrono::steady_clock::now(); };
    bool expireHosts();  // true when something expired
    QTimer timer_;
    QLineEdit* filter_;
    QCheckBox* listening_;
    QComboBox *proto_, *state_;
    QLabel *note_, *status_;
    QTableView* table_;
    ConnectionsModel* model_;
    QPushButton *go_, *copy_;
    std::size_t namesSig_ = 0;
    int sortColumn_ = 0;
    bool descending_ = false;
};

}  // namespace pmp::gui
