// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/live_view.hpp>
#include <perfmonplus/gui/treemap_widget.hpp>
#include <perfmonplus/linux/command.hpp>
#include <perfmonplus/linux/disk_scan.hpp>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace pmp::gui {

// Disk Space (§7.12): pick a volume or folder, scan it in the background, and see where the space went. The scan
// never blocks the window or the sampler. Nothing is ever deleted: "Move to Trash" hands the path to `gio trash`
// after a confirmation whose default is Cancel.
class DiskSpacePage : public QWidget, public LiveView {
    Q_OBJECT
public:
    using ScanFn = std::function<linux::ScanResult(const linux::ScanOptions&, const std::atomic<bool>&,
                                                   const std::function<void(const linux::ScanProgress&)>&)>;
    using MountsFn = std::function<std::vector<linux::MountInfo>()>;
    struct Hooks {
        std::function<bool(const QString& title, const QString& text)> confirm;  // default: a box whose default is Cancel
        std::function<QString()> chooseFolder;                                   // default: a folder dialog
        std::function<void(const QString& dir)> reveal;                          // default: xdg-open
        linux::CommandRunner run;                                                // default: runDefault (for gio trash)
    };

    DiskSpacePage(ScanFn scan, MountsFn mounts, QWidget* parent = nullptr);
    ~DiskSpacePage() override;

    void update(const Frame&, const HistoryStore&, MonoTime) override {}
    void setTheme(const vm::Theme&, bool) override {}
    bool animates() const override { return false; }
    void setHooks(Hooks h);
    void setScanBlocked(bool blocked, const QString& reason);  // a running benchmark blocks scans (BEN-03)

    void scanPath(const QString& path);  // starts a scan of this folder
    void cancelScan();
    bool scanning() const { return scanning_; }
    std::shared_ptr<const linux::ScanResult> result() const { return result_; }

    QComboBox* volumeBox() const { return volume_; }
    QPushButton* scanButton() const { return scan_; }
    QPushButton* chooseButton() const { return choose_; }
    QTabWidget* tabs() const { return tabs_; }
    TreemapWidget* treemap() const { return map_; }
    QTableWidget* largestTable() const { return largest_; }
    QTableWidget* typesTable() const { return types_; }
    QLineEdit* filterEdit() const { return filter_; }
    QString progressText() const { return progress_->text(); }
    QString summaryText() const { return summary_->text(); }
    QString summaryTip() const { return summary_->toolTip(); }
    QString breadcrumbText() const { return crumbs_->text(); }
    QString statusText() const { return status_->text(); }
    QPushButton* revealButton() const { return reveal_; }
    QPushButton* trashButton() const { return trash_; }
    QPushButton* copyButton() const { return copy_; }
    QString selectedPath() const { return selectedPath_; }
    void selectPath(const QString& path);
    void trashSelected();
    void revealSelected();
    void openFolder(const linux::ScanNode* node);  // drill into a folder of the map
    void up();                                     // back to the parent folder

signals:
    void scanFinished();
    void scanRunningChanged(bool running);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void finishScan(std::shared_ptr<linux::ScanResult> r);
    void showResult();
    void fillLargest();
    void fillTypes();
    QString pathOf(const linux::ScanNode* n) const;
    bool removeFromResult(const QString& path);
    void updateButtons();

    ScanFn scanFn_;
    MountsFn mountsFn_;
    Hooks hooks_;
    std::shared_ptr<linux::ScanResult> result_;
    std::vector<const linux::ScanNode*> trail_;  // from the scan root down to the folder on show
    std::string rootPath_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    bool scanning_ = false;
    bool blocked_ = false;
    QString blockReason_;
    QString selectedPath_;
    QComboBox* volume_;
    QPushButton *scan_, *choose_, *reveal_, *trash_, *copy_;
    QLabel *progress_, *summary_, *crumbs_, *status_;
    QTabWidget* tabs_;
    TreemapWidget* map_;
    QTableWidget *largest_, *types_;
    QLineEdit* filter_;
    int sortColumn_ = 1;
    bool descending_ = true;
};

}  // namespace pmp::gui
