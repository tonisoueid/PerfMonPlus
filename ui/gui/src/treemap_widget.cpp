// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/treemap_widget.hpp>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/gui/plain_text.hpp>

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace pmp::gui {
namespace {

QColor colourFor(int index, int depth) {
    const int hue = (index * 47) % 360;
    return QColor::fromHsl(hue, depth == 0 ? 120 : 90, depth == 0 ? 90 : 70);
}

}  // namespace

TreemapWidget::TreemapWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAccessibleName("Folder size map");
    setMinimumSize(200, 140);
}

void TreemapWidget::setNode(const linux::ScanNode* node) {
    node_ = node;
    selected_ = nullptr;
    relayout();
}

void TreemapWidget::select(const linux::ScanNode* n) {
    selected_ = n;
    update();
}

void TreemapWidget::relayout() {
    rects_ = node_ ? vm::layoutTreemap(*node_, width(), height()) : std::vector<vm::TreemapRect>{};
    // A spoken summary for assistive technology: the five biggest items.
    QString text;
    if (node_) {
        int n = 0;
        for (const auto& c : node_->children) {
            if (c.isOther || n >= 5) continue;
            text += QString("%1 %2. ").arg(QString::fromStdString(c.name), QString::fromStdString(pmp::format(Metric::measured(static_cast<double>(c.bytes), Unit::Bytes))));
            ++n;
        }
    }
    setAccessibleDescription(text);
    update();
}

void TreemapWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    relayout();
}

QString TreemapWidget::tooltipFor(const linux::ScanNode* n) const {
    if (!n) return {};
    const double parent = node_ && node_->bytes > 0 ? static_cast<double>(node_->bytes) : 1.0;
    // Qt shows a tooltip as rich text when it looks like markup, and the name comes from the disk: escape it and say so.
    return plainTip(QString("%1 · %2 · %3% of this folder")
                        .arg(QString::fromStdString(n->name),
                             QString::fromStdString(pmp::format(Metric::measured(static_cast<double>(n->bytes), Unit::Bytes))))
                        .arg(static_cast<double>(n->bytes) / parent * 100.0, 0, 'f', 1));
}

void TreemapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    if (!node_ || rects_.empty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, node_ ? "Nothing to show here" : "Choose a folder and press Scan");
        return;
    }
    for (const auto& r : rects_) {
        const int top = r.group;
        const QRectF box(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
        p.setPen(QPen(palette().color(QPalette::Window), 1));
        p.setBrush(r.node ? colourFor(top, r.depth) : QColor(70, 70, 78));
        p.drawRect(box);
        if (r.node && r.node == selected_) {
            p.setPen(QPen(Qt::white, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box.adjusted(1, 1, -1, -1));
        }
        if (box.width() > 48 && box.height() > 14) {
            p.setPen(Qt::white);
            const auto label = p.fontMetrics().elidedText(QString::fromStdString(r.label), Qt::ElideRight, static_cast<int>(box.width()) - 6);
            p.drawText(box.adjusted(3, 1, -3, -1), Qt::AlignLeft | Qt::AlignTop, label);  // drawText never interprets markup
        }
    }
}

void TreemapWidget::mousePressEvent(QMouseEvent* e) {
    const auto* n = vm::hitTest(rects_, e->position().x(), e->position().y());
    if (!n) return;
    selected_ = n;
    update();
    emit nodeSelected(n);
}

void TreemapWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    const auto* n = vm::hitTest(rects_, e->position().x(), e->position().y());
    if (n && n->isDir && !n->children.empty()) emit nodeOpened(n);
}

bool TreemapWidget::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        const auto* he = static_cast<QHelpEvent*>(e);
        const auto* n = vm::hitTest(rects_, he->pos().x(), he->pos().y());
        if (n) QToolTip::showText(he->globalPos(), tooltipFor(n), this);
        else QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

}  // namespace pmp::gui
