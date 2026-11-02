// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/meter_widgets.hpp>

#include <perfmonplus/core/format.hpp>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QToolTip>
#include <QVBoxLayout>

#include <time.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace pmp::gui {
namespace {

constexpr double kMargin = 4;  // room for the bloom halo
constexpr double kSegGap = 2;
constexpr double kCaptionHeight = 16;

QColor mix(QColor a, QColor b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

}  // namespace

// ---- SegmentBar -----------------------------------------------------------

SegmentBar::SegmentBar(vm::MeterKind kind, Qt::Orientation orientation, int segments, QWidget* parent)
    : QWidget(parent), kind_(kind), orientation_(orientation), segments_(std::max(1, segments)) {
    // paintEvent fills the whole rect, so Qt need not repaint the (stylesheet-drawn) parent underneath.
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SegmentBar::setTheme(const vm::Theme& theme) {
    theme_ = theme;
    update();
}

void SegmentBar::setBloom(bool on) {
    bloom_ = on;
    update();
}

void SegmentBar::setMetric(const Metric& m, double fullScale) {
    target_ = vm::litSegments(m, fullScale, segments_);  // the display catches up in tick(); no repaint yet
    if (!m.isMeasured() && litCount() != 0) {  // an unavailable reading must never look like a value: drop at once
        shown_ = 0;
        update();
    }
}

int SegmentBar::litCount() const { return static_cast<int>(std::lround(shown_)); }

void SegmentBar::settleForTest() {
    shown_ = target_;
    update();
}

void SegmentBar::tick(double dtSeconds) {
    const int before = litCount();
    shown_ = vm::easeToward(shown_, target_, dtSeconds, 0.10);
    if (std::abs(shown_ - target_) < 0.02) shown_ = target_;
    if (litCount() != before) update();  // the bar shows whole segments: a fractional move draws the same picture
}

QSize SegmentBar::sizeHint() const {
    return orientation_ == Qt::Horizontal ? QSize(240, 26) : QSize(26, 200);
}

QRectF SegmentBar::segmentRect(int i) const {
    if (orientation_ == Qt::Horizontal) {
        const double avail = width() - 2 * kMargin;
        const double w = (avail - (segments_ - 1) * kSegGap) / segments_;
        return QRectF(kMargin + i * (w + kSegGap), kMargin, w, height() - 2 * kMargin);
    }
    const double avail = height() - 2 * kMargin;
    const double h = (avail - (segments_ - 1) * kSegGap) / segments_;
    return QRectF(kMargin, height() - kMargin - (i + 1) * h - i * kSegGap, width() - 2 * kMargin, h);
}

void SegmentBar::paintEvent(QPaintEvent*) {
    ++paintCount_;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), toQColor(theme_.panel));
    const int lit = litCount();
    const QColor panel = toQColor(theme_.panel);
    for (int i = 0; i < segments_; ++i) {
        const QColor c = toQColor(vm::segmentColor(theme_, kind_, i, segments_));
        const QRectF r = segmentRect(i);
        if (i < lit) {
            if (bloom_) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(c.red(), c.green(), c.blue(), 55));
                p.drawRoundedRect(r.adjusted(-3, -3, 3, 3), 4, 4);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(c);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(mix(panel, c, 0.14));
        }
        p.drawRoundedRect(r, 2, 2);
    }
}

// ---- HistoryGraph ---------------------------------------------------------

HistoryGraph::HistoryGraph(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);  // as SegmentBar: the whole rect is painted here
}

void HistoryGraph::setTheme(const vm::Theme& theme) {
    theme_ = theme;
    update();
}
void HistoryGraph::setBloom(bool on) {
    bloom_ = on;
    update();
}
void HistoryGraph::addSeries(const HistoryRing<double>* ring, vm::Rgb colour) {
    series_.push_back({ring, colour, QString(), std::nullopt});
    update();
}
void HistoryGraph::addSeries(const HistoryRing<double>* ring, vm::Rgb colour, const QString& label, Unit unit) {
    series_.push_back({ring, colour, label, unit});
    update();
}

void HistoryGraph::setCursorHub(GraphCursorHub* hub) {
    if (hub_) disconnect(hub_, nullptr, this, nullptr);
    hub_ = hub;
    setMouseTracking(hub != nullptr);
    if (hub_) connect(hub_, &GraphCursorHub::cursorChanged, this, [this] { update(); });
}

std::optional<MonoTime> HistoryGraph::cursorTime() const { return hub_ ? hub_->cursor() : std::nullopt; }

QString HistoryGraph::hoverText() const {
    const auto t = cursorTime();
    if (!t) return {};
    // The ring stores steady-clock times; the clocks' current offset turns one into a time of day.
    const auto wall = std::chrono::system_clock::now() - (std::chrono::steady_clock::now() - *t);
    const std::time_t tt = std::chrono::system_clock::to_time_t(wall);
    tm parts{};
    localtime_r(&tt, &parts);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &parts);
    QString text = QString::fromLatin1(buf);
    const double pxMs = std::chrono::duration<double, std::milli>(window_).count() / std::max(1.0, plotRect().width());
    const auto tolerance = std::chrono::milliseconds(std::max<long long>(1500, static_cast<long long>(pxMs * 8)));
    for (const auto& s : series_) {
        if (!s.unit) continue;
        const auto near = vm::sampleNear(*s.ring, *t, tolerance);
        const QString value = near && near->value ? QString::fromStdString(format(Metric::measured(*near->value, *s.unit))) : QString("—");
        text += "\n" + (s.label.isEmpty() ? caption_ : s.label) + "  " + value;
    }
    return text;
}

void HistoryGraph::setSelectable(bool on) {
    selectable_ = on;
    setFocusPolicy(on ? Qt::ClickFocus : Qt::NoFocus);
    if (!on) selection_.reset();
    update();
}

std::optional<std::pair<MonoTime, MonoTime>> HistoryGraph::selection() const {
    if (!selection_ || !vm::clipToWindow(*selection_, now_, window_)) return std::nullopt;
    return std::pair{selection_->from, selection_->to};
}

void HistoryGraph::mousePressEvent(QMouseEvent* e) {
    if (!selectable_ || e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const QRectF plot = plotRect();
    pressX_ = std::clamp(e->position().x() - plot.left(), 0.0, plot.width());
    pressTime_ = vm::timeAtX(pressX_, {plot.width(), plot.height()}, now_, window_);
    dragging_ = false;
}

void HistoryGraph::mouseMoveEvent(QMouseEvent* e) {
    const QRectF plot = plotRect();
    if (selectable_ && pressTime_ && (e->buttons() & Qt::LeftButton)) {
        const double x = std::clamp(e->position().x() - plot.left(), 0.0, plot.width());  // dragging past the edge sticks to it
        if (!dragging_ && std::abs(x - pressX_) >= 4.0) dragging_ = true;
        if (dragging_) {
            if (const auto t = vm::timeAtX(x, {plot.width(), plot.height()}, now_, window_)) selection_ = vm::orderedRange(*pressTime_, *t);
            update();
        }
    }
    if (!hub_) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    const auto t = vm::timeAtX(e->position().x() - plot.left(), {plot.width(), plot.height()}, now_, window_);
    if (!t || e->position().y() < plot.top() || e->position().y() > plot.bottom()) {
        hub_->setCursor(std::nullopt);
        QToolTip::hideText();
        return;
    }
    const bool changed = hub_->cursor() != t;
    hub_->setCursor(t);
    if (changed && !dragging_) {
        // Escaped and wrapped so a label can never be mistaken for markup.
        QToolTip::showText(e->globalPosition().toPoint(), "<p style='white-space:pre'>" + hoverText().toHtmlEscaped() + "</p>", this);
    }
}

void HistoryGraph::mouseReleaseEvent(QMouseEvent* e) {
    if (!selectable_ || !pressTime_) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    if (dragging_ && selection_) {
        emit rangeSelected(selection_->from, selection_->to, e->globalPosition().toPoint());
    } else {
        selection_.reset();  // a plain click clears
        update();
    }
    pressTime_.reset();
    dragging_ = false;
}

void HistoryGraph::keyPressEvent(QKeyEvent* e) {
    if (selectable_ && e->key() == Qt::Key_Escape) {
        selection_.reset();
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void HistoryGraph::leaveEvent(QEvent* e) {
    if (hub_) hub_->setCursor(std::nullopt);
    QToolTip::hideText();
    QWidget::leaveEvent(e);
}

void HistoryGraph::clearSeries() {
    series_.clear();
    update();
}
void HistoryGraph::setFixedScale(vm::Scale scale) {
    fixed_ = scale;
    update();
}
void HistoryGraph::setAutoScale(double minSpan) {
    fixed_.reset();
    minSpan_ = minSpan;
    update();
}
void HistoryGraph::setNow(MonoTime now) {
    now_ = now;
    if (selection_ && !vm::clipToWindow(*selection_, now_, window_)) selection_.reset();  // scrolled out of view
    // The line only moves window/width seconds per pixel (60 s over ~600 px is ~10 px/s), so redrawing
    // every animation frame is wasted work. Repaint once it has moved `quantumPx_` pixels (see vm::scrollQuantumPx).
    const double secondsPerPixel = std::chrono::duration<double>(window_).count() / std::max(1.0, plotRect().width());
    const double moved = std::abs(std::chrono::duration<double>(now - paintedNow_).count());
    if (!painted_ || moved >= secondsPerPixel * quantumPx_) update();
}
void HistoryGraph::setWindow(std::chrono::milliseconds window) {
    window_ = window;
    update();
}
void HistoryGraph::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

void HistoryGraph::setBorderColor(std::optional<vm::Rgb> colour) {
    border_ = colour;
    update();
}

QRectF HistoryGraph::plotRect() const {
    const double captionH = caption_.isEmpty() ? 0 : kCaptionHeight;
    return QRectF(1, 1, width() - 2, height() - 2 - captionH);
}

void HistoryGraph::paintEvent(QPaintEvent*) {
    ++paintCount_;
    paintedNow_ = now_;
    painted_ = true;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), toQColor(theme_.background));

    const QRectF plot = plotRect();
    p.fillRect(plot, toQColor(theme_.panel));

    // Grid: quarters horizontally, sixths of the window vertically.
    p.setPen(QPen(toQColor(theme_.grid), 1));
    for (int i = 1; i < 4; ++i) {
        const double y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int i = 1; i < 6; ++i) {
        const double x = plot.left() + plot.width() * i / 6.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    vm::Scale scale = fixed_.value_or(vm::Scale{0, 1});
    if (!fixed_) {
        double hi = minSpan_;
        for (const auto& s : series_) hi = std::max(hi, vm::autoScale(*s.ring, now_, window_, minSpan_).hi);
        scale = {0, hi};
    }

    p.save();
    p.setClipRect(plot);
    p.translate(plot.topLeft());
    for (const auto& s : series_) {
        const auto trace = vm::buildTrace(*s.ring, now_, window_, {plot.width(), plot.height()}, scale);
        const QColor c = toQColor(s.colour);
        // Time with no data (a suspend, a stopped provider) is marked, so a small graph never looks like a short dip.
        for (std::size_t i = 1; i < trace.runs.size(); ++i) {
            if (trace.runs[i - 1].empty() || trace.runs[i].empty()) continue;
            const double x0 = trace.runs[i - 1].back().x;
            const double x1 = trace.runs[i].front().x;
            p.fillRect(QRectF(x0, 0, std::max(3.0, x1 - x0), plot.height()), toQColor(theme_.textDim, 70));
        }
        for (const auto& run : trace.runs) {
            QPolygonF poly;
            for (const auto& pt : run) poly << QPointF(pt.x, pt.y);
            if (poly.size() == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawEllipse(poly[0], 1.5, 1.5);
                continue;
            }
            if (bloom_) {
                p.setPen(QPen(QColor(c.red(), c.green(), c.blue(), 50), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPolyline(poly);
            }
            p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(poly);
        }
    }
    p.restore();

    p.setPen(QPen(toQColor(border_.value_or(theme_.border)), border_ ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    if (const auto sel = selection_ ? vm::clipToWindow(*selection_, now_, window_) : std::nullopt) {
        const auto x0 = vm::xAtTime(sel->from, {plot.width(), plot.height()}, now_, window_);
        const auto x1 = vm::xAtTime(sel->to, {plot.width(), plot.height()}, now_, window_);
        if (x0 && x1) p.fillRect(QRectF(plot.left() + *x0, plot.top(), std::max(1.0, *x1 - *x0), plot.height()), toQColor(theme_.accent, 60));
    }
    if (hub_ && hub_->cursor()) {
        if (const auto x = vm::xAtTime(*hub_->cursor(), {plot.width(), plot.height()}, now_, window_)) {
            p.setPen(QPen(toQColor(theme_.text, 150), 1));
            p.drawLine(QPointF(plot.left() + *x, plot.top()), QPointF(plot.left() + *x, plot.bottom()));
        }
    }

    if (!caption_.isEmpty()) {
        p.setPen(toQColor(theme_.textDim));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        p.drawText(QRectF(2, plot.bottom() + 1, width() - 4, kCaptionHeight), Qt::AlignLeft | Qt::AlignVCenter, caption_);
    }
}

// ---- StatBlock --------------------------------------------------------------

StatBlock::StatBlock(const QString& caption, QWidget* parent)
    : QWidget(parent), caption_(new QLabel(caption, this)), value_(new QLabel("—", this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    caption_->setFont(small);
    QFont big = font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    value_->setFont(big);
    layout->addWidget(caption_);
    layout->addWidget(value_);
    setTheme(vm::darkTheme());
}

void StatBlock::setValue(const QString& text) { value_->setText(text); }
QString StatBlock::value() const { return value_->text(); }

void StatBlock::setTheme(const vm::Theme& theme) {
    QPalette pc = caption_->palette();
    pc.setColor(QPalette::WindowText, toQColor(theme.textDim));
    caption_->setPalette(pc);
    QPalette pv = value_->palette();
    pv.setColor(QPalette::WindowText, toQColor(theme.text));
    value_->setPalette(pv);
}

}  // namespace pmp::gui
