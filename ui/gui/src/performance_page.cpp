// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/performance_page.hpp>

#include <perfmonplus/gui/clock_source.hpp>

#include <QEvent>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/vm/system_model.hpp>

#include <QHBoxLayout>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>

namespace pmp::gui {
namespace {

constexpr int kCellMinWidth = 110;
constexpr int kCellHeight = 80;
constexpr int kCellGap = 6;
constexpr int kStatColumns = 4;
// The graph window from Settings; one main window per process, only touched on the UI thread.
std::chrono::seconds g_window{60};

QString q(const std::string& s) { return QString::fromStdString(s); }

QString coreClassName(CoreClass c) {
    switch (c) {
        case CoreClass::Performance: return "performance";
        case CoreClass::Efficiency: return "efficiency";
        case CoreClass::Unknown: return "unknown";
    }
    return "unknown";
}

vm::MeterKind kindOf(vm::Subsystem s) {
    switch (s) {
        case vm::Subsystem::Cpu: return vm::MeterKind::Cpu;
        case vm::Subsystem::Memory: return vm::MeterKind::Memory;
        case vm::Subsystem::Disks: return vm::MeterKind::Disk;
        case vm::Subsystem::Network: return vm::MeterKind::Network;
        case vm::Subsystem::Gpu: return vm::MeterKind::Gpu;
        case vm::Subsystem::Npu: return vm::MeterKind::Gpu;
        case vm::Subsystem::Energy: return vm::MeterKind::Energy;
        case vm::Subsystem::Thermals: return vm::MeterKind::Thermal;
    }
    return vm::MeterKind::Cpu;
}

}  // namespace

// ---- MasterCard ---------------------------------------------------------------

MasterCard::MasterCard(vm::Subsystem which, int device, QWidget* parent)
    : QFrame(parent),
      which_(which),
      device_(device),
      title_(new QLabel(this)),
      line1_(new QLabel(this)),
      line2_(new QLabel(this)),
      spark_(new HistoryGraph(this)) {
    setObjectName("card");
    setCursor(Qt::PointingHandCursor);
    QFont f = title_->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    title_->setFont(f);
    line1_->setObjectName("dim");
    line2_->setObjectName("dim");
    spark_->setFixedSize(84, 56);
    spark_->setFixedScale({0, 100});
    auto* text = new QVBoxLayout;
    text->setSpacing(0);
    text->addWidget(title_);
    text->addWidget(line1_);
    text->addWidget(line2_);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 6, 10, 6);
    row->addLayout(text, 1);
    row->addWidget(spark_);
}

void MasterCard::set(const vm::MasterEntry& e) {
    title_->setText(q(e.title));
    line1_->setText(q(e.line1));
    line2_->setText(q(e.line2));
}

void MasterCard::setSelected(bool selected) {
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
}

// ---- CpuGrid ---------------------------------------------------------------------

CpuGrid::CpuGrid(QWidget* parent) : QWidget(parent) {}

void CpuGrid::setTheme(const vm::Theme& theme, bool bloom) {
    theme_ = theme;
    bloom_ = bloom;
    for (auto* c : cells_) {
        c->setTheme(theme);
        c->setBloom(bloom);
    }
}

void CpuGrid::update(const Frame& f, const HistoryStore& history, MonoTime now) {
    const std::size_t n = f.cpu.logical.size();
    while (cells_.size() < n) {
        auto* g = new HistoryGraph(this);
        g->setFixedScale({0, 100});
        g->setTheme(theme_);
        g->setBloom(false);  // 24+ glowing tiles would dominate the CPU budget
        g->show();
        cells_.push_back(g);
    }
    while (cells_.size() > n) {
        delete cells_.back();
        cells_.pop_back();
    }
    for (std::size_t i = 0; i < n; ++i) {
        auto* g = cells_[i];
        const auto& l = f.cpu.logical[i];
        g->clearSeries();
        if (const auto* ring = history.logicalCpuById(l.id)) g->addSeries(ring, theme_.cpu);  // by id: offline cores shift positions
        g->setNow(now);
        g->setWindow(g_window);
        g->setCaption(QString("CPU %1").arg(l.id));
        g->setProperty("cpu", l.id);
        g->setProperty("coreClass", coreClassName(l.coreClass));
        g->setBorderColor(l.coreClass == CoreClass::Performance  ? std::optional<vm::Rgb>(theme_.netRx)
                          : l.coreClass == CoreClass::Efficiency ? std::optional<vm::Rgb>(theme_.disk)
                                                                 : std::nullopt);
    }
    relayout();
}

void CpuGrid::relayout() {
    const auto cells = vm::gridLayout(static_cast<int>(cells_.size()), width(), kCellMinWidth, kCellHeight, kCellGap);
    int bottom = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cells_[i]->setGeometry(cells[i].x, cells[i].y, cells[i].w, cells[i].h);
        bottom = std::max(bottom, cells[i].y + cells[i].h);
    }
    setMinimumHeight(bottom);
}

// ---- SensorGrid ---------------------------------------------------------------------

SensorGrid::SensorGrid(QWidget* parent) : QWidget(parent) {}

void SensorGrid::setTheme(const vm::Theme& theme, bool bloom) {
    theme_ = theme;
    for (auto* c : cells_) {
        c->setTheme(theme);
        c->setBloom(bloom);
    }
}

void SensorGrid::update(const Frame& f, const HistoryStore& history, MonoTime now) {
    const std::size_t n = f.thermals.sensors.size();
    while (cells_.size() < n) {
        auto* g = new HistoryGraph(this);
        g->setFixedScale({0, 110});
        g->setTheme(theme_);
        g->setBloom(false);
        g->show();
        cells_.push_back(g);
    }
    while (cells_.size() > n) {
        delete cells_.back();
        cells_.pop_back();
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = f.thermals.sensors[i];
        auto* g = cells_[i];
        g->clearSeries();
        try {
            g->addSeries(&history.sensor(s.id), theme_.temperature);
        } catch (const std::out_of_range&) {  // sensor not ingested yet
        }
        g->setNow(now);
        g->setWindow(g_window);
        g->setCaption(q(s.label) + "  " + q(format(s.celsius)));
        g->setProperty("sensor", q(s.id));
    }
    relayout();
}

void SensorGrid::relayout() {
    const auto cells = vm::gridLayout(static_cast<int>(cells_.size()), width(), 180, kCellHeight, kCellGap);
    int bottom = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cells_[i]->setGeometry(cells[i].x, cells[i].y, cells[i].w, cells[i].h);
        bottom = std::max(bottom, cells[i].y + cells[i].h);
    }
    setMinimumHeight(bottom);
}

// ---- DetailPanel -------------------------------------------------------------------

DetailPanel::DetailPanel(vm::Subsystem which, int device, QWidget* parent)
    : QWidget(parent),
      which_(which),
      device_(device),
      title_(new QLabel(this)),
      subtitle_(new QLabel("—", this)),
      value_(new QLabel("—", this)),
      bar_(new SegmentBar(kindOf(which), Qt::Horizontal, 30, this)),
      statsLayout_(new QGridLayout) {
    const char* names[] = {"CPU", "Memory", "Disks", "Network", "GPU", "NPU", "Energy", "Thermals"};
    title_->setText(which == vm::Subsystem::Gpu || which == vm::Subsystem::Npu
                        ? QString("%1 %2").arg(names[static_cast<int>(which)]).arg(device)
                        : QString(names[static_cast<int>(which)]));
    QFont tf = title_->font();
    tf.setPointSizeF(tf.pointSizeF() * 2.4);
    title_->setFont(tf);
    QFont vf = value_->font();
    vf.setPointSizeF(vf.pointSizeF() * 1.8);
    value_->setFont(vf);
    subtitle_->setObjectName("dim");
    value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value_->setMinimumWidth(150);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 8, 14, 8);
    auto* head = new QHBoxLayout;
    head->addWidget(title_);
    head->addStretch(1);
    head->addWidget(value_);
    col->addLayout(head);
    col->addWidget(subtitle_);
    bar_->setMinimumHeight(24);
    col->addWidget(bar_);

    const auto addGraph = [&](const QString& caption, int stretch) {
        auto* cap = new QLabel(caption, this);
        cap->setObjectName("dim");
        col->addWidget(cap);
        auto* g = new HistoryGraph(this);
        g->setMinimumHeight(110);
        g->setWindow(g_window);
        g->setCursorHub(cursorHub_);  // the graphs of one panel share a hover cursor
        col->addWidget(g, stretch);
        graphCaptions_.push_back(cap);
        graphs_.push_back(g);
    };

    switch (which) {
        case vm::Subsystem::Cpu: {
            auto* cap = new QLabel("% Utilization by logical processor", this);
            cap->setObjectName("dim");
            col->addWidget(cap);
            auto* scroll = new QScrollArea(this);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            cpuGrid_ = new CpuGrid;
            scroll->setWidget(cpuGrid_);
            cpuGrid_->setAutoFillBackground(false);  // QScrollArea fills it with the palette Window colour otherwise
            scroll->viewport()->setAutoFillBackground(false);
            col->addWidget(scroll, 1);
            break;
        }
        case vm::Subsystem::Memory: addGraph("Memory utilization", 1); break;
        case vm::Subsystem::Disks:
            addGraph("% Active time", 1);
            addGraph("Disk transfer rate", 1);
            break;
        case vm::Subsystem::Network: addGraph("Throughput", 1); break;
        case vm::Subsystem::Gpu:
            addGraph("GPU utilization", 1);
            partialNote_ = new QLabel("Includes only processes you can inspect; other users' GPU work is not counted.", this);
            partialNote_->setObjectName("dim");
            partialNote_->setWordWrap(true);
            partialNote_->hide();
            col->addWidget(partialNote_);
            break;
        case vm::Subsystem::Npu: addGraph("NPU active time", 1); break;
        case vm::Subsystem::Energy: addGraph("Power consumption", 1); break;
        case vm::Subsystem::Thermals: addGraph("CPU / SoC hotspot", 1); break;
    }
    if (which == vm::Subsystem::Energy || which == vm::Subsystem::Thermals) {
        auto* chipRow = new QHBoxLayout;
        for (int i = 0; i < (which == vm::Subsystem::Energy ? 3 : 2); ++i) {
            auto* chip = new QLabel(this);
            chip->setObjectName("chip");
            chips_.push_back(chip);
            chipRow->addWidget(chip);
        }
        chipRow->addStretch(1);
        col->addLayout(chipRow);
    }
    col->addLayout(statsLayout_);

    if (which == vm::Subsystem::Energy) {
        auto* title = new QLabel("Processes with the highest estimated energy demand", this);
        QFont f = title->font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        title->setFont(f);
        col->addWidget(title);
        for (int i = 0; i < 8; ++i) {
            DemandRow row{new QLabel(this), new QProgressBar(this), new QLabel(this)};
            row.name->setTextFormat(Qt::PlainText);  // a process name is not markup
            row.bar->setRange(0, 100);
            row.bar->setTextVisible(false);
            row.value->setMinimumWidth(48);
            row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto* line = new QHBoxLayout;
            row.name->setMinimumWidth(160);
            row.name->setCursor(Qt::PointingHandCursor);
            row.name->setToolTip("Show in Processes");
            row.name->installEventFilter(this);
            line->addWidget(row.name);
            line->addWidget(row.bar, 1);
            line->addWidget(row.value);
            col->addLayout(line);
            demand_.push_back(row);
        }
        footnote_ = new QLabel(this);
        footnote_->setObjectName("dim");
        footnote_->setWordWrap(true);
        restrictedHelp_ = new QLabel(this);
        restrictedHelp_->setWordWrap(true);
        restrictedHelp_->hide();
        col->addWidget(footnote_);
        col->addWidget(restrictedHelp_);
    }
    if (which == vm::Subsystem::Thermals) {
        auto* cap = new QLabel("Temperature sensors", this);
        QFont f = cap->font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        cap->setFont(f);
        col->addWidget(cap);
        noSensors_ = new QLabel("No sensors available on this system", this);
        noSensors_->setObjectName("dim");
        noSensors_->hide();
        col->addWidget(noSensors_);
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sensorGrid_ = new SensorGrid;
        scroll->setWidget(sensorGrid_);
        sensorGrid_->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setMinimumHeight(170);
        col->addWidget(scroll, 1);
    }
}

void DetailPanel::setTheme(const vm::Theme& theme, bool bloom) {
    theme_ = theme;
    bloom_ = bloom;
    bar_->setTheme(theme);
    bar_->setBloom(bloom);
    for (auto* g : graphs_) {
        g->setTheme(theme);
        g->setBloom(bloom);
    }
    if (cpuGrid_) cpuGrid_->setTheme(theme, bloom);
    if (sensorGrid_) sensorGrid_->setTheme(theme, bloom);
    for (auto* s : stats_) s->setTheme(theme);
}

void DetailPanel::setStats(const std::vector<vm::StatItem>& items) {
    while (stats_.size() < items.size()) {
        const int i = static_cast<int>(stats_.size());
        auto* s = new StatBlock(q(items[stats_.size()].caption), this);
        s->setTheme(theme_);
        statsLayout_->addWidget(s, i / kStatColumns, i % kStatColumns);
        stats_.push_back(s);
    }
    for (std::size_t i = 0; i < items.size(); ++i) stats_[i]->setValue(q(items[i].value));
}

QString DetailPanel::statValue(const QString& caption) const {
    for (const auto* s : stats_) {
        if (s->caption() == caption) return s->value();
    }
    return {};
}

void DetailPanel::update(const Frame& f, const HistoryStore& h, MonoTime now, const vm::MasterEntry& entry) {
    for (auto* g : graphs_) {
        g->clearSeries();
        g->setNow(now);
    }
    const auto minute = g_window;

    switch (which_) {
        case vm::Subsystem::Cpu:
            value_->setText(q(entry.line1));
            subtitle_->setText(q(entry.line2));
            bar_->setMetric(f.cpu.total, 100);
            cpuGrid_->update(f, h, now);
            {
                auto items = vm::cpuStats(f);
                items.insert(items.end(), cpuFacts_.begin(), cpuFacts_.end());
                setStats(items);
            }
            break;
        case vm::Subsystem::Memory: {
            value_->setText(q(entry.line2));
            subtitle_->setText(f.memory.total.isMeasured() ? q(format(f.memory.total)) + " total" : "Unavailable");
            Metric pct = Metric::notMeasured(Availability::Pending, Unit::Percent);
            if (f.memory.inUse.isMeasured() && f.memory.total.isMeasured() && *f.memory.total.value > 0) {
                pct = Metric::measured(*f.memory.inUse.value / *f.memory.total.value * 100.0, Unit::Percent);
            }
            bar_->setMetric(pct, 100);
            graphs_[0]->addSeries(&h.series(Series::MemoryUsedBytes), theme_.memory, "In use", Unit::Bytes);
            if (f.memory.total.isMeasured()) graphs_[0]->setFixedScale({0, *f.memory.total.value});
            else graphs_[0]->setAutoScale(1.0);
            setStats(vm::memoryStats(f));
            break;
        }
        case vm::Subsystem::Disks:
            value_->setText(q(entry.line2));
            subtitle_->setText(q(entry.line1));
            bar_->setMetric(f.disks.combined.activePct, 100);
            graphs_[0]->addSeries(&h.series(Series::DiskActivePct), theme_.disk, "Active time", Unit::Percent);
            graphs_[0]->setFixedScale({0, 100});
            graphs_[1]->addSeries(&h.series(Series::DiskReadBps), theme_.disk, "Read", Unit::BytesPerSec);
            graphs_[1]->addSeries(&h.series(Series::DiskWriteBps), theme_.energy, "Write", Unit::BytesPerSec);
            graphs_[1]->setAutoScale(1024.0 * 1024.0);
            setStats(vm::diskStats(f));
            break;
        case vm::Subsystem::Network: {
            const auto& n = f.net.combined;
            Metric total = n.rxBps.isMeasured() && n.txBps.isMeasured()
                               ? Metric::measured(*n.rxBps.value + *n.txBps.value, Unit::BytesPerSec)
                               : Metric::notMeasured(Availability::Pending, Unit::BytesPerSec);
            value_->setText(q(format(total)));
            subtitle_->setText(q(entry.line1));
            const auto rx = vm::autoScale(h.series(Series::NetRxBps), now, minute, 128.0 * 1024);
            const auto tx = vm::autoScale(h.series(Series::NetTxBps), now, minute, 128.0 * 1024);
            bar_->setMetric(total, rx.hi + tx.hi);
            graphs_[0]->addSeries(&h.series(Series::NetRxBps), theme_.netRx, "Receive", Unit::BytesPerSec);
            graphs_[0]->addSeries(&h.series(Series::NetTxBps), theme_.netTx, "Send", Unit::BytesPerSec);
            graphs_[0]->setAutoScale(128.0 * 1024);
            setStats(vm::networkStats(f));
            break;
        }
        case vm::Subsystem::Gpu:
            if (static_cast<std::size_t>(device_) < f.gpus.size()) {
                const auto& g = f.gpus[static_cast<std::size_t>(device_)];
                value_->setText(q(format(g.util)));
                subtitle_->setText(q(g.name));
                bar_->setMetric(g.util, 100);
                if (static_cast<std::size_t>(device_) < h.gpuCount()) {
                    graphs_[0]->addSeries(&h.gpuUtil(static_cast<std::size_t>(device_)), theme_.netRx, "GPU", Unit::Percent);
                }
                graphs_[0]->setFixedScale({0, 100});
                setStats(vm::gpuStats(g));
                partialNote_->setVisible(g.visibleClientsOnly);
            }
            break;
        case vm::Subsystem::Energy: {
            value_->setText(q(format(f.energy.watts)));
            subtitle_->setText(q(entry.line1));
            const auto scale = vm::autoScale(h.series(Series::PowerW), now, minute, 25.0);
            bar_->setMetric(f.energy.watts, scale.hi);
            graphs_[0]->addSeries(&h.series(Series::PowerW), theme_.energy, "Power", Unit::Watts);
            graphs_[0]->setAutoScale(25.0);
            const auto chips = vm::energyChips(f);
            for (std::size_t i = 0; i < chips_.size() && i < chips.size(); ++i) chips_[i]->setText(q(chips[i].caption + ": " + chips[i].value));
            setStats(vm::batteryStats(f.energy));
            const auto rows = vm::energyDemand(f, demand_.size());
            demandKeys_.clear();
            for (const auto& r : rows) demandKeys_.push_back(r.key);
            for (std::size_t i = 0; i < demand_.size(); ++i) {
                const bool have = i < rows.size();
                demand_[i].name->setVisible(have);
                demand_[i].bar->setVisible(have);
                demand_[i].value->setVisible(have);
                if (have) {
                    demand_[i].name->setText(q(rows[i].name));
                    demand_[i].bar->setValue(static_cast<int>(rows[i].score));
                    demand_[i].value->setText(q(rows[i].value));
                }
            }
            footnote_->setText(q(vm::energyFootnote(f)));
            const auto help = vm::restrictedPowerHelp(f.energy);
            restrictedHelp_->setText(q(help));
            restrictedHelp_->setVisible(!help.empty());
            break;
        }
        case vm::Subsystem::Thermals: {
            value_->setText(q(format(f.thermals.hotspot)));
            subtitle_->setText(q(entry.line1));
            bar_->setMetric(f.thermals.hotspot, 110);
            graphs_[0]->addSeries(&h.series(Series::HotspotC), theme_.temperature, "Hotspot", Unit::Celsius);
            graphs_[0]->setFixedScale({0, 110});
            const auto chips = vm::thermalChips(f);
            for (std::size_t i = 0; i < chips_.size() && i < chips.size(); ++i) chips_[i]->setText(q(chips[i].caption + ": " + chips[i].value));
            sensorGrid_->update(f, h, now);
            noSensors_->setVisible(f.thermals.sensors.empty());
            std::vector<vm::StatItem> fans;
            for (const auto& fan : f.thermals.fans) fans.push_back({fan.label, format(fan.rpm)});
            setStats(fans);
            break;
        }
        case vm::Subsystem::Npu:
            if (static_cast<std::size_t>(device_) < f.npus.size()) {
                const auto& n = f.npus[static_cast<std::size_t>(device_)];
                value_->setText(q(format(n.activePct)));
                subtitle_->setText(q(n.name));
                bar_->setMetric(n.activePct, 100);
                if (static_cast<std::size_t>(device_) < h.npuCount()) {
                    graphs_[0]->addSeries(&h.npuUtil(static_cast<std::size_t>(device_)), theme_.cpuKernel, "NPU", Unit::Percent);
                }
                graphs_[0]->setFixedScale({0, 100});
                setStats(vm::npuStats(n));
            }
            break;
        default: break;
    }
}

bool DetailPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress && processClicked) {
        for (std::size_t i = 0; i < demand_.size(); ++i) {
            if (watched == demand_[i].name && i < demandKeys_.size()) {
                processClicked(demandKeys_[i]);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DetailPanel::tick(double dt) {
    bar_->tick(dt);
    const auto now = graphNow();
    for (auto* g : graphs_) g->setNow(now);  // main graphs scroll smoothly; per-core tiles step once a second
}

// ---- PerformancePage -----------------------------------------------------------------

PerformancePage::PerformancePage(QWidget* parent) : QWidget(parent), stack_(new QStackedWidget(this)) {
    connect(culprits_, &CulpritsPresenter::processRequested, this, &PerformancePage::processRequested);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 4, 14, 10);
    row->setSpacing(10);

    leftLayout_ = new QVBoxLayout;
    leftLayout_->setSpacing(8);
    leftLayout_->addStretch(1);
    row->addLayout(leftLayout_);

    auto* detail = new QFrame(this);
    detail->setObjectName("card");
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(stack_);
    row->addWidget(detail, 1);

    // The four subsystems every machine has; hardware cards are added as frames report them.
    syncPanels({{vm::Subsystem::Cpu, 0, "CPU", "—", "—"},
                {vm::Subsystem::Memory, 0, "Memory", "Unavailable", "—"},
                {vm::Subsystem::Disks, 0, "Disks", "Unavailable", "—"},
                {vm::Subsystem::Network, 0, "Network", "Unavailable", "—"}});
    setTheme(theme_, bloom_);
}

// Creates, removes and orders cards and panels so they equal the hardware that reports. The selection is kept by
// key; if its hardware went away, CPU is shown.
void PerformancePage::syncPanels(const std::vector<vm::MasterEntry>& entries) {
    std::vector<Key> want;
    for (const auto& e : entries) want.push_back({e.which, e.device});
    if (want == keys_) return;

    QList<MasterCard*> cards;
    std::vector<DetailPanel*> panels;
    for (std::size_t i = 0; i < want.size(); ++i) {
        const auto old = std::find(keys_.begin(), keys_.end(), want[i]);
        if (old != keys_.end()) {
            const auto idx = static_cast<std::size_t>(old - keys_.begin());
            cards.push_back(cards_[static_cast<qsizetype>(idx)]);
            panels.push_back(panels_[idx]);
            continue;
        }
        auto* card = new MasterCard(want[i].which, want[i].device, this);
        card->setFixedWidth(340);
        card->spark()->setTheme(theme_);
        card->spark()->setBloom(false);
        auto* panel = new DetailPanel(want[i].which, want[i].device, stack_);
        panel->setTheme(theme_, bloom_);
        panel->processClicked = [this](ProcessKey key) { emit processRequested(key); };
        if (want[i].which == vm::Subsystem::Memory && panel->mainGraph()) culprits_->attach(panel->mainGraph(), ProcessHistory::Resource::Memory);
        stack_->addWidget(panel);
        const Key key = want[i];
        connect(card, &MasterCard::clicked, this, [this, key] { selectSubsystem(key.which, key.device); });
        cards.push_back(card);
        panels.push_back(panel);
    }
    for (std::size_t i = 0; i < keys_.size(); ++i) {  // hardware that went away
        if (std::find(want.begin(), want.end(), keys_[i]) == want.end()) {
            cards_[static_cast<qsizetype>(i)]->deleteLater();
            stack_->removeWidget(panels_[i]);
            panels_[i]->deleteLater();
        }
    }
    while (QLayoutItem* item = leftLayout_->takeAt(0)) delete item;
    for (auto* c : cards) {
        c->show();
        leftLayout_->addWidget(c);
    }
    leftLayout_->addStretch(1);

    keys_ = std::move(want);
    cards_ = std::move(cards);
    panels_ = std::move(panels);
    if (std::find(keys_.begin(), keys_.end(), selectedKey_) == keys_.end()) selectedKey_ = {vm::Subsystem::Cpu, 0};
    select(selectedIndex());
}

int PerformancePage::selectedIndex() const {
    const auto it = std::find(keys_.begin(), keys_.end(), selectedKey_);
    return it == keys_.end() ? 0 : static_cast<int>(it - keys_.begin());
}

void PerformancePage::selectSubsystem(vm::Subsystem which, int device) {
    const auto it = std::find(keys_.begin(), keys_.end(), Key{which, device});
    if (it != keys_.end()) select(static_cast<int>(it - keys_.begin()));
}

void PerformancePage::select(int index) {
    selectedKey_ = keys_[static_cast<std::size_t>(index)];
    stack_->setCurrentWidget(panels_[static_cast<std::size_t>(index)]);
    for (int i = 0; i < cards_.size(); ++i) cards_[i]->setSelected(i == index);
    refresh();
}

const std::vector<HistoryGraph*>& PerformancePage::sensorTiles() const {
    static const std::vector<HistoryGraph*> none;
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i].which == vm::Subsystem::Thermals && panels_[i]->sensorGrid()) return panels_[i]->sensorGrid()->cells();
    }
    return none;
}

QString PerformancePage::detailTitle() const { return panels_[static_cast<std::size_t>(selectedIndex())]->title(); }

QString PerformancePage::statValue(const QString& caption) const {
    return panels_[static_cast<std::size_t>(selectedIndex())]->statValue(caption);
}

void PerformancePage::refresh() {
    if (!history_ || entries_.size() != panels_.size()) return;
    const auto i = static_cast<std::size_t>(selectedIndex());
    panels_[i]->update(last_, *history_, now_, entries_[i]);
}

void PerformancePage::update(const Frame& f, const HistoryStore& history, MonoTime now) {
    last_ = f;
    history_ = &history;
    now_ = now;
    culprits_->setLogicalCpus(static_cast<int>(f.cpu.logical.size()));

    entries_ = vm::masterEntries(f);
    syncPanels(entries_);
    for (int i = 0; i < cards_.size(); ++i) {
        auto* card = cards_[i];
        card->set(entries_[static_cast<std::size_t>(i)]);
        card->spark()->clearSeries();
        card->spark()->setNow(now);
        switch (card->subsystem()) {
            case vm::Subsystem::Cpu: card->spark()->addSeries(&history.series(Series::CpuTotal), theme_.cpu); break;
            case vm::Subsystem::Memory:
                card->spark()->addSeries(&history.series(Series::MemoryUsedBytes), theme_.memory);
                if (f.memory.total.isMeasured()) card->spark()->setFixedScale({0, *f.memory.total.value});
                else card->spark()->setAutoScale(1.0);
                break;
            case vm::Subsystem::Disks: card->spark()->addSeries(&history.series(Series::DiskActivePct), theme_.disk); break;
            case vm::Subsystem::Network:
                card->spark()->addSeries(&history.series(Series::NetRxBps), theme_.netRx);
                card->spark()->addSeries(&history.series(Series::NetTxBps), theme_.netTx);
                card->spark()->setAutoScale(128.0 * 1024);
                break;
            case vm::Subsystem::Gpu:
                if (static_cast<std::size_t>(card->device()) < history.gpuCount()) {
                    card->spark()->addSeries(&history.gpuUtil(static_cast<std::size_t>(card->device())), theme_.netRx);
                }
                card->spark()->setFixedScale({0, 100});
                break;
            case vm::Subsystem::Npu:
                if (static_cast<std::size_t>(card->device()) < history.npuCount()) {
                    card->spark()->addSeries(&history.npuUtil(static_cast<std::size_t>(card->device())), theme_.cpuKernel);
                }
                card->spark()->setFixedScale({0, 100});
                break;
            case vm::Subsystem::Energy:
                card->spark()->addSeries(&history.series(Series::PowerW), theme_.energy);
                card->spark()->setAutoScale(25.0);
                break;
            case vm::Subsystem::Thermals:
                card->spark()->addSeries(&history.series(Series::HotspotC), theme_.temperature);
                card->spark()->setFixedScale({0, 110});
                break;
        }
    }
    refresh();
}

void DetailPanel::setAnimationRate(int hz) {
    for (auto* g : graphs_) g->setScrollQuantum(vm::scrollQuantumPx(hz));
}

void PerformancePage::setSystemInfo(const linux::SystemInfo& info) {
    panels_[0]->setCpuFacts(vm::cpuFactItems(info.cpu));  // CPU is always the first panel
    refresh();
}

void PerformancePage::setAnimationRate(int hz) {
    for (auto* p : panels_) p->setAnimationRate(hz);
}

void PerformancePage::setHistoryWindow(int seconds) {
    g_window = std::chrono::seconds(seconds);
    for (auto* g : findChildren<HistoryGraph*>()) g->setWindow(g_window);
}

void PerformancePage::tick(double dt) { panels_[static_cast<std::size_t>(selectedIndex())]->tick(dt); }

void PerformancePage::setTheme(const vm::Theme& theme, bool bloom) {
    theme_ = theme;
    bloom_ = bloom;
    for (auto* c : cards_) {
        c->spark()->setTheme(theme);
        c->spark()->setBloom(false);
    }
    for (auto* p : panels_) p->setTheme(theme, bloom);
}

}  // namespace pmp::gui
