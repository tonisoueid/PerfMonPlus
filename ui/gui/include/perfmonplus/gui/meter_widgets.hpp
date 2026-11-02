// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/history_ring.hpp>
#include <perfmonplus/core/reading.hpp>
#include <perfmonplus/gui/graph_cursor.hpp>
#include <perfmonplus/vm/graph_geometry.hpp>
#include <perfmonplus/vm/meter.hpp>
#include <perfmonplus/vm/theme.hpp>

#include <QColor>
#include <QLabel>
#include <QWidget>

#include <chrono>
#include <optional>
#include <vector>

namespace pmp::gui {

inline QColor toQColor(vm::Rgb c, int alpha = 255) { return QColor(c.r, c.g, c.b, alpha); }

// Segmented VFD-style meter (VIS-01/02). Owns no data: a page pushes the latest reading in.
class SegmentBar : public QWidget {
    Q_OBJECT
public:
    SegmentBar(vm::MeterKind kind, Qt::Orientation orientation, int segments, QWidget* parent = nullptr);

    void setTheme(const vm::Theme& theme);
    void setBloom(bool on);
    void setMetric(const Metric& m, double fullScale);  // sets the target; the display eases toward it
    int litCount() const;                                // segments currently displayed lit
    int paintCount() const { return paintCount_; }
    void settleForTest();                                // jump the display to its target
    QRectF segmentRect(int index) const;
    QSize sizeHint() const override;

public slots:
    void tick(double dtSeconds);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    vm::MeterKind kind_;
    Qt::Orientation orientation_;
    int segments_;
    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    int target_ = 0;
    double shown_ = 0;
    int paintCount_ = 0;
};

// Scrolling history graph (VIS-03). Series are borrowed, not owned.
class HistoryGraph : public QWidget {
    Q_OBJECT
public:
    explicit HistoryGraph(QWidget* parent = nullptr);

    void setTheme(const vm::Theme& theme);
    void setBloom(bool on);
    void addSeries(const HistoryRing<double>* ring, vm::Rgb colour);
    // With a name and a unit the series also appears in the hover tooltip ("Utilization  42.0%").
    void addSeries(const HistoryRing<double>* ring, vm::Rgb colour, const QString& label, Unit unit);
    // Shares one hover cursor with the other graphs of the page and turns hovering on (HIS-02).
    void setCursorHub(GraphCursorHub* hub);
    std::optional<MonoTime> cursorTime() const;
    QString hoverText() const;  // "14:02:11" and one line per named series; "—" where the series has a gap

    // Drag across the graph to select a stretch of time (HIS-03); a click or Escape clears it. The selection is
    // kept as absolute times, so it scrolls out of view with the data, and it never pauses sampling (HIS-04).
    void setSelectable(bool on);
    std::optional<std::pair<MonoTime, MonoTime>> selection() const;  // nullopt once it has left the window
    void clearSeries();
    void setFixedScale(vm::Scale scale);
    void setAutoScale(double minSpan);
    void setNow(MonoTime now);
    void setScrollQuantum(double pixels) { quantumPx_ = pixels; }
    void setWindow(std::chrono::milliseconds window);
    void setCaption(const QString& caption);
signals:
    void rangeSelected(MonoTime from, MonoTime to, QPoint globalPos);

public:
    void setBorderColor(std::optional<vm::Rgb> colour);  // e.g. tint a per-core tile; nullopt = theme border
    QRectF plotRect() const;
    int seriesCount() const { return static_cast<int>(series_.size()); }
    int paintCount() const { return paintCount_; }  // lets tests assert that idle graphs are not redrawn
    QSize sizeHint() const override { return {240, 120}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    struct SeriesRef {
        const HistoryRing<double>* ring;
        vm::Rgb colour;
        QString label;
        std::optional<Unit> unit;
    };
    GraphCursorHub* hub_ = nullptr;
    bool selectable_ = false;
    std::optional<vm::TimeRange> selection_;
    std::optional<MonoTime> pressTime_;
    double pressX_ = 0;
    bool dragging_ = false;
    vm::Theme theme_ = vm::darkTheme();
    bool bloom_ = true;
    std::vector<SeriesRef> series_;
    std::optional<vm::Scale> fixed_;
    double minSpan_ = 1.0;
    MonoTime now_{};
    std::chrono::milliseconds window_{60000};
    QString caption_;
    std::optional<vm::Rgb> border_;
    double quantumPx_ = 0.5;
    int paintCount_ = 0;
    MonoTime paintedNow_{};
    bool painted_ = false;
};

// Small dim caption above a large value (VIS-06).
class StatBlock : public QWidget {
    Q_OBJECT
public:
    explicit StatBlock(const QString& caption, QWidget* parent = nullptr);
    void setValue(const QString& text);
    QString value() const;
    QString caption() const { return caption_->text(); }
    void setTheme(const vm::Theme& theme);

private:
    QLabel* caption_;
    QLabel* value_;
};

}  // namespace pmp::gui
