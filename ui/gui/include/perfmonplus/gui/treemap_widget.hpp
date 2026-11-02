// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/disk_scan.hpp>
#include <perfmonplus/vm/treemap.hpp>

#include <QWidget>

namespace pmp::gui {

// The folder-size map (SPC-02): one rectangle per item, area proportional to size. Click selects, double-click on a
// folder opens it, and the tooltip reads "name · size · % of parent". All text is drawn, never interpreted.
class TreemapWidget : public QWidget {
    Q_OBJECT
public:
    explicit TreemapWidget(QWidget* parent = nullptr);

    void setNode(const linux::ScanNode* node);  // the folder to show; null clears the map
    const linux::ScanNode* node() const { return node_; }
    const std::vector<vm::TreemapRect>& rects() const { return rects_; }
    const linux::ScanNode* selected() const { return selected_; }
    void select(const linux::ScanNode* n);
    QString descriptionText() const { return accessibleDescription(); }
    QString tooltipFor(const linux::ScanNode* n) const;
    QSize sizeHint() const override { return {640, 380}; }

signals:
    void nodeSelected(const pmp::linux::ScanNode* node);
    void nodeOpened(const pmp::linux::ScanNode* node);  // double click on a folder

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    bool event(QEvent* e) override;

private:
    void relayout();

    const linux::ScanNode* node_ = nullptr;
    const linux::ScanNode* selected_ = nullptr;
    std::vector<vm::TreemapRect> rects_;
};

}  // namespace pmp::gui
