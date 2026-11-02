// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/linux/command.hpp>
#include <perfmonplus/linux/installed_apps.hpp>
#include <perfmonplus/linux/kernel_modules.hpp>
#include <perfmonplus/vm/apps_model.hpp>

#include <QAbstractTableModel>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

namespace pmp::gui {

class AppsTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit AppsTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    void setRows(std::vector<vm::AppRow> rows);
    void setLoading(bool loading);  // sizes read "Calculating…" until the list is complete (APP-01)
    const vm::AppRow& rowAt(int i) const { return rows_.at(static_cast<std::size_t>(i)); }
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& = {}) const override { return 8; }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    static const char* sortKey(int column);

private:
    std::vector<vm::AppRow> rows_;
    bool loading_ = false;
};

// A stacked bar of known sizes per source (APP-02). Plain painting: no text from outside is interpreted.
class SizeBar : public QWidget {
    Q_OBJECT
public:
    explicit SizeBar(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(14); }
    void setData(std::vector<std::pair<QString, double>> parts);
    QSize sizeHint() const override { return {400, 14}; }
    int segmentCount() const { return static_cast<int>(parts_.size()); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<std::pair<QString, double>> parts_;
};

// Installed Apps (§7.11) with a Kernel modules tab (DRV-02). Nothing is read until the page is first shown. Removing
// an app never runs with extra rights: a per-user Flatpak is removed after a confirmation, everything else shows the
// command to run yourself.
class InstalledAppsPage : public QWidget, public LiveView {
    Q_OBJECT
public:
    using CollectFn = std::function<std::vector<linux::InstalledApp>(const std::atomic<bool>&, std::vector<std::string>*)>;
    using ModulesFn = std::function<std::vector<linux::KernelModule>()>;
    struct Hooks {
        std::function<bool(const QString& title, const QString& text)> confirm;  // default: a box whose default is Cancel
        std::function<void(const QString& command)> showCommand;                 // default: a box with Copy command
        std::function<void(const QString& path)> reveal;                         // default: xdg-open
    };

    InstalledAppsPage(CollectFn collect, linux::CommandRunner run, ModulesFn modules, QWidget* parent = nullptr);

    void update(const Frame&, const HistoryStore&, MonoTime) override {}
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }
    void setHooks(Hooks h);
    // Finds an app's folder on demand (apt packages have none recorded): used by Reveal.
    void setLocator(std::function<std::string(const linux::InstalledApp&)> locator) {
        locator_ = std::move(locator);
        updateButtons();
    }

    void refresh();      // reads off the UI thread; Cancel stops it
    void refreshSync();  // tests and screenshots
    void cancel();
    bool loading() const { return loading_; }
    bool everLoaded() const { return loadedOnce_; }

    QTabWidget* tabs() const { return tabs_; }
    QTableView* table() const { return table_; }
    AppsTableModel* model() const { return model_; }
    QLineEdit* filterEdit() const { return filter_; }
    QComboBox* sourceBox() const { return source_; }
    SizeBar* sizeBar() const { return bar_; }
    QString totalText() const { return total_->text(); }
    QString noteText() const { return note_->text(); }
    QString statusText() const { return status_->text(); }
    QPushButton* uninstallButton() const { return uninstall_; }
    QPushButton* revealButton() const { return reveal_; }
    QPushButton* copyButton() const { return copy_; }
    QPushButton* cancelButton() const { return cancelBtn_; }
    QTableWidget* modulesTable() const { return modules_; }
    QString copyText() const;
    void uninstallSelected();
    void revealSelected();

protected:
    void showEvent(QShowEvent* e) override;

private:
    void apply(std::vector<linux::InstalledApp> apps, std::vector<std::string> skipped);
    void rebuild();
    void loadModules();
    void fillModules();
    const linux::InstalledApp* selectedApp() const;
    void updateButtons();

    CollectFn collect_;
    linux::CommandRunner run_;
    ModulesFn modulesFn_;
    Hooks hooks_;
    std::function<std::string(const linux::InstalledApp&)> locator_;
    std::vector<linux::InstalledApp> apps_;
    std::vector<linux::KernelModule> mods_;
    std::shared_ptr<std::atomic<bool>> cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    bool loading_ = false;
    bool loadedOnce_ = false;
    int sortColumn_ = 4;
    bool descending_ = true;
    QTabWidget* tabs_;
    QLineEdit* filter_;
    QComboBox* source_;
    QLabel *total_, *note_, *status_;
    SizeBar* bar_;
    QTableView* table_;
    AppsTableModel* model_;
    QPushButton *uninstall_, *reveal_, *copy_, *cancelBtn_;
    QTableWidget* modules_;
    QLineEdit* modulesFilter_;
};

}  // namespace pmp::gui
