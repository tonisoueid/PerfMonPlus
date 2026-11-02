// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/power_freq_page.hpp>

#include <perfmonplus/vm/performance_model.hpp>

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace pmp::gui {
namespace {

constexpr int kCellMinWidth = 150;
constexpr int kCellHeight = 90;
constexpr int kCellGap = 6;

QString q(const std::string& s) { return QString::fromStdString(s); }

QLabel* section(const QString& title, QWidget* parent) {
    auto* l = new QLabel(title, parent);
    l->setObjectName("dim");
    return l;
}

QString spokenHertz(const Metric& m) {
    if (!m.isMeasured()) return QStringLiteral("unavailable");
    return *m.value >= 1e9 ? QStringLiteral("%1 gigahertz").arg(*m.value / 1e9, 0, 'f', 2)
                           : QStringLiteral("%1 megahertz").arg(*m.value / 1e6, 0, 'f', 0);
}

}  // namespace

PowerFreqPage::PowerFreqPage(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    col->setSpacing(8);

    col->addWidget(section("Frequency", this));
    freqHost_ = new QWidget(this);
    col->addWidget(freqHost_);

    auto* stats = new QHBoxLayout;
    governor_ = new StatBlock("Governor", this);
    epp_ = new StatBlock("Energy preference", this);
    driver_ = new StatBlock("Driver", this);
    throttle_ = new StatBlock("Throttling", this);
    for (auto* s : {governor_, epp_, driver_, throttle_}) stats->addWidget(s);
    stats->addStretch(1);
    col->addLayout(stats);

    col->addWidget(section("Power", this));
    powerNote_ = new QLabel(this);
    powerNote_->setObjectName("dim");
    powerNote_->setTextFormat(Qt::PlainText);
    col->addWidget(powerNote_);
    powerHost_ = new QWidget(this);
    col->addWidget(powerHost_);
    help_ = new QLabel(this);
    help_->setObjectName("dim");
    help_->setTextFormat(Qt::PlainText);
    help_->setWordWrap(true);
    help_->hide();
    col->addWidget(help_);

    batteryTitle_ = section("Battery", this);
    col->addWidget(batteryTitle_);
    battery_ = new QLabel(this);
    battery_->setTextFormat(Qt::PlainText);
    col->addWidget(battery_);
    col->addStretch(1);
    batteryTitle_->hide();
    battery_->hide();
    setTheme(theme_, bloom_);
}

void PowerFreqPage::setTheme(const vm::Theme& theme, bool bloom) {
    theme_ = theme;
    bloom_ = bloom;
    for (auto* g : freqCells_) {
        g->setTheme(theme);
        g->setBloom(false);
    }
    for (auto* g : powerCells_) {
        g->setTheme(theme);
        g->setBloom(false);
    }
    for (auto* s : {governor_, epp_, driver_, throttle_}) s->setTheme(theme);
}

void PowerFreqPage::setHistoryWindow(int seconds) {
    window_ = std::chrono::seconds(std::max(1, seconds));
    for (auto* g : freqCells_) g->setWindow(window_);
    for (auto* g : powerCells_) g->setWindow(window_);
}

void PowerFreqPage::syncCells(std::vector<HistoryGraph*>& cells, QWidget* host, std::size_t n) {
    while (cells.size() < n) {
        auto* g = new HistoryGraph(host);
        g->setTheme(theme_);
        g->setBloom(false);  // many small tiles: glow would dominate the CPU budget
        g->setWindow(window_);
        g->show();
        cells.push_back(g);
    }
    while (cells.size() > n) {
        delete cells.back();
        cells.pop_back();
    }
}

void PowerFreqPage::update(const Frame& f, const HistoryStore& history, MonoTime now) {
    const auto m = vm::buildPowerFreq(f);

    // ---- frequency: one tile per core, coloured by core class ----
    const std::size_t nCores = std::min(m.cores.size(), f.cpu.logical.size());
    syncCells(freqCells_, freqHost_, nCores);
    double maxHz = 0;
    for (const auto& c : f.cpu.logical) {
        if (c.freqMaxHz.isMeasured()) maxHz = std::max(maxHz, *c.freqMaxHz.value);
    }
    for (std::size_t i = 0; i < nCores; ++i) {
        auto* g = freqCells_[i];
        const auto& row = m.cores[i];
        g->clearSeries();
        if (const auto* ring = history.logicalFreqById(f.cpu.logical[i].id)) g->addSeries(ring, theme_.cpu, q(row.label), Unit::Hertz);
        if (maxHz > 0) g->setFixedScale({0, maxHz});
        else g->setAutoScale(1e9);
        g->setNow(now);
        g->setWindow(window_);
        g->setCaption(q(row.label + "  " + row.cur));
        g->setAccessibleName(QStringLiteral("%1 frequency %2").arg(q(row.label), spokenHertz(f.cpu.logical[i].freqHz)));
        g->setProperty("coreClass", static_cast<int>(row.coreClass));
        g->setBorderColor(row.coreClass == CoreClass::Performance  ? std::optional<vm::Rgb>(theme_.netRx)
                          : row.coreClass == CoreClass::Efficiency ? std::optional<vm::Rgb>(theme_.disk)
                                                                   : std::nullopt);
    }

    governor_->setValue(q(m.governor));
    epp_->setValue(q(m.epp));
    driver_->setValue(q(m.driver));
    throttle_->setValue(q(m.throttle));

    // ---- power: one tile per domain ----
    std::size_t nDom = 0;
    for (const auto& d : m.domains) {
        try {
            (void)history.domain(d.id);
            ++nDom;
        } catch (const std::out_of_range&) {
        }
    }
    syncCells(powerCells_, powerHost_, nDom);
    std::size_t k = 0;
    for (const auto& d : m.domains) {
        if (k >= nDom) break;
        try {
            const auto& ring = history.domain(d.id);
            auto* g = powerCells_[k++];
            g->clearSeries();
            g->addSeries(&ring, theme_.energy, q(d.label), Unit::Watts);
            g->setAutoScale(5.0);
            g->setNow(now);
            g->setWindow(window_);
            g->setCaption(q(d.label + "  " + d.value));
            g->setAccessibleName(QStringLiteral("%1 power %2").arg(q(d.label), q(d.value)));
        } catch (const std::out_of_range&) {
        }
    }
    powerNote_->setText(m.domains.empty() ? QStringLiteral("This system does not report power by domain.") : QString());
    powerNote_->setVisible(m.domains.empty());
    help_->setText(q(m.restrictedHelp));
    help_->setVisible(!m.restrictedHelp.empty());

    QStringList lines;
    for (const auto& [k2, v] : m.battery.rows) lines << q(k2) + ": " + q(v);
    battery_->setText(lines.join('\n'));
    battery_->setVisible(!lines.isEmpty());
    batteryTitle_->setVisible(!lines.isEmpty());
    relayout();
}

void PowerFreqPage::relayout() {
    auto place = [&](QWidget* host, std::vector<HistoryGraph*>& cells) {
        const auto grid = vm::gridLayout(static_cast<int>(cells.size()), std::max(host->width(), kCellMinWidth), kCellMinWidth,
                                         kCellHeight, kCellGap);
        int bottom = 0;
        for (std::size_t i = 0; i < grid.size(); ++i) {
            cells[i]->setGeometry(grid[i].x, grid[i].y, grid[i].w, grid[i].h);
            bottom = std::max(bottom, grid[i].y + grid[i].h);
        }
        host->setMinimumHeight(bottom);
    };
    place(freqHost_, freqCells_);
    place(powerHost_, powerCells_);
}

void PowerFreqPage::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    relayout();
}

}  // namespace pmp::gui
