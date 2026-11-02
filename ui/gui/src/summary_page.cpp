// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/summary_page.hpp>

#include <perfmonplus/gui/plain_text.hpp>

#include <perfmonplus/gui/clock_source.hpp>

#include <perfmonplus/core/format.hpp>

#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pmp::gui {
namespace {

constexpr int kNameColumn = 1;
const char* const kColumnTitles[] = {"PID", "Name", "CPU", "GPU", "Memory"};
constexpr int kColumnWidths[] = {52, 130, 52, 30, 86};

QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    return card;
}

QLabel* titleLabel(const QString& text, QWidget* parent, double scale = 1.5) {
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * scale);
    l->setFont(f);
    return l;
}

QLabel* dimLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName("dim");
    return l;
}

QString countText(const Metric& m) {
    return m.isMeasured() ? QString::number(static_cast<long long>(*m.value)) : QStringLiteral("—");
}

QString q(const std::string& s) { return QString::fromStdString(s); }

}  // namespace

// ---- TileCard ---------------------------------------------------------------

TileCard::TileCard(const QString& title, vm::MeterKind kind, QWidget* parent)
    : QFrame(parent),
      title_(title),
      value_(new QLabel("—", this)),
      subtitle_(new QLabel(this)),
      bar_(new SegmentBar(kind, Qt::Horizontal, 30, this)) {
    setObjectName("card");
    setCursor(Qt::PointingHandCursor);
    auto* head = new QHBoxLayout;
    head->addWidget(titleLabel(title, this, 1.3));
    head->addStretch(1);
    QFont f = value_->font();
    f.setBold(true);
    value_->setFont(f);
    head->addWidget(value_);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 8, 12, 8);
    col->addLayout(head);
    subtitle_->setObjectName("dim");
    col->addWidget(subtitle_);
    bar_->setMinimumHeight(22);
    col->addWidget(bar_);
}

void TileCard::set(const vm::TileText& text, const Metric& fill, double fullScale) {
    value_->setText(q(text.value));
    subtitle_->setText(q(text.subtitle));
    bar_->setMetric(fill, fullScale);
}

void TileCard::setTheme(const vm::Theme& theme, bool bloom) {
    bar_->setTheme(theme);
    bar_->setBloom(bloom);
}

// ---- SummaryPage -------------------------------------------------------------

SummaryPage::SummaryPage(QWidget* parent)
    : QWidget(parent),
      cpuValue_(new QLabel("—", this)),
      cpuFooterLeft_(dimLabel("", this)),
      cpuFooterRight_(dimLabel("", this)),
      cpuGraph_(new HistoryGraph(this)),
      totalProcesses_(dimLabel("— total", this)),
      table_(new QTableWidget(0, 5, this)),
      memBar_(new SegmentBar(vm::MeterKind::Memory, Qt::Vertical, 30, this)),
      memPct_(new QLabel("—", this)),
      memHeader_(new QLabel("Unavailable", this)),
      memGraph_(new HistoryGraph(this)),
      memAvailable_(dimLabel("", this)),
      memCached_(dimLabel("", this)),
      memSwap_(dimLabel("", this)),
      diskTile_(new TileCard("Disks", vm::MeterKind::Disk, this)),
      netTile_(new TileCard("Network", vm::MeterKind::Network, this)) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 4, 14, 10);
    root->setSpacing(10);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < static_cast<int>(topKeys_.size())) emit processRequested(topKeys_[static_cast<std::size_t>(row)]);
    });
    culprits_->attach(cpuGraph_, ProcessHistory::Resource::Cpu);
    culprits_->attach(memGraph_, ProcessHistory::Resource::Memory);
    connect(culprits_, &CulpritsPresenter::processRequested, this, &SummaryPage::processRequested);
    auto* cursor = new GraphCursorHub(this);  // CPU and memory show the same moment under the pointer
    cpuGraph_->setCursorHub(cursor);
    memGraph_->setCursorHub(cursor);

    // --- top row: VFD stack | CPU overview | top processes
    auto* top = new QHBoxLayout;
    top->setSpacing(10);

    vfdCard_ = makeCard(this);
    auto* vfdCard = vfdCard_;
    auto* vfdRow = new QHBoxLayout(vfdCard);
    vfdRow->setContentsMargins(10, 8, 10, 8);
    const char* labels[] = {"CPU", "Clock", "Temp", "GPU"};
    const vm::MeterKind kinds[] = {vm::MeterKind::Cpu, vm::MeterKind::Energy, vm::MeterKind::Thermal, vm::MeterKind::Gpu};
    for (int i = 0; i < 4; ++i) {
        auto* colWidget = new QWidget(vfdCard);
        vfdColumns_[static_cast<std::size_t>(i)] = colWidget;
        auto* col = new QVBoxLayout(colWidget);
        col->setContentsMargins(0, 0, 0, 0);
        auto* name = dimLabel(labels[i], colWidget);
        name->setAlignment(Qt::AlignHCenter);
        vfdBars_[static_cast<std::size_t>(i)] = new SegmentBar(kinds[i], Qt::Vertical, 25, colWidget);
        vfdBars_[static_cast<std::size_t>(i)]->setFixedWidth(34);
        vfdBars_[static_cast<std::size_t>(i)]->setMinimumHeight(170);
        auto* value = dimLabel("—", colWidget);
        value->setAlignment(Qt::AlignHCenter);
        value->setFixedWidth(66);
        value->setWordWrap(true);
        vfdValues_[static_cast<std::size_t>(i)] = value;
        col->addWidget(name);
        col->addWidget(vfdBars_[static_cast<std::size_t>(i)], 1, Qt::AlignHCenter);
        col->addWidget(value, 0, Qt::AlignHCenter);
        vfdRow->addWidget(colWidget);
    }
    vfdCard->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    top->addWidget(vfdCard);

    auto* cpuCard = makeCard(this);
    auto* cpuCol = new QVBoxLayout(cpuCard);
    cpuCol->setContentsMargins(12, 8, 12, 8);
    auto* cpuHead = new QHBoxLayout;
    cpuHead->addWidget(titleLabel("CPU Overview", cpuCard));
    cpuHead->addStretch(1);
    QFont big = cpuValue_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    cpuValue_->setFont(big);
    cpuHead->addWidget(cpuValue_);
    cpuCol->addLayout(cpuHead);
    auto* legendRow = new QHBoxLayout;
    const char* legendNames[] = {"Utilization", "Kernel", "Temperature"};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(legendNames[i], cpuCard);
        b->setCheckable(true);
        b->setChecked(true);
        b->setFlat(true);
        legend_[static_cast<std::size_t>(i)] = b;
        legendRow->addWidget(b);
        connect(b, &QPushButton::toggled, this, [this] { applyCpuSeries(); });
    }
    legendRow->addStretch(1);
    cpuCol->addLayout(legendRow);
    cpuGraph_->setParent(cpuCard);
    cpuGraph_->setMinimumHeight(150);
    cpuGraph_->setFixedScale({0, 100});
    cpuCol->addWidget(cpuGraph_, 1);
    auto* cpuFoot = new QHBoxLayout;
    cpuFoot->addWidget(cpuFooterLeft_);
    cpuFoot->addStretch(1);
    cpuFoot->addWidget(cpuFooterRight_);
    cpuCol->addLayout(cpuFoot);
    cpuCard->setMinimumWidth(320);
    top->addWidget(cpuCard, 1);

    auto* procCard = makeCard(this);
    auto* procCol = new QVBoxLayout(procCard);
    procCol->setContentsMargins(12, 8, 12, 8);
    auto* procHead = new QHBoxLayout;
    procTitle_ = titleLabel(QString("Top CPU processes (%1)").arg(topRows_), procCard, 1.2);
    procHead->addWidget(procTitle_);
    procHead->addStretch(1);
    totalProcesses_->setParent(procCard);
    procHead->addWidget(totalProcesses_);
    procCol->addLayout(procHead);
    table_->setParent(procCard);
    table_->setHorizontalHeaderLabels({kColumnTitles[0], kColumnTitles[1], kColumnTitles[2], kColumnTitles[3], kColumnTitles[4]});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->setShowGrid(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setMinimumSectionSize(20);  // the default (~38 px) would widen the narrow GPU column
    for (int c = 0; c < 5; ++c) table_->setColumnWidth(c, kColumnWidths[c]);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int tableWidth = 2;
    for (int w : kColumnWidths) tableWidth += w;
    table_->setFixedWidth(tableWidth);
    table_->setMinimumHeight(24 + topRows_ * 22 + 4);
    procCol->addWidget(table_);
    procCard->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    top->addWidget(procCard);
    root->addLayout(top, 3);

    // --- memory card
    auto* memCard = makeCard(this);
    auto* memRow = new QHBoxLayout(memCard);
    memRow->setContentsMargins(12, 8, 12, 8);
    auto* memLeft = new QVBoxLayout;
    memBar_->setFixedWidth(34);
    memBar_->setMinimumHeight(120);
    memLeft->addWidget(memBar_, 1, Qt::AlignHCenter);
    memPct_->setObjectName("dim");
    memPct_->setAlignment(Qt::AlignHCenter);
    memLeft->addWidget(memPct_);
    memRow->addLayout(memLeft);
    auto* memCol = new QVBoxLayout;
    auto* memHead = new QHBoxLayout;
    memHead->addWidget(titleLabel("Memory Utilization", memCard));
    memHead->addStretch(1);
    memHead->addWidget(memHeader_);
    memCol->addLayout(memHead);
    memGraph_->setMinimumHeight(90);
    memCol->addWidget(memGraph_, 1);
    auto* memFoot = new QHBoxLayout;
    memFoot->addWidget(memAvailable_);
    memFoot->addStretch(1);
    memFoot->addWidget(memCached_);
    memFoot->addStretch(1);
    memFoot->addWidget(memSwap_);
    memCol->addLayout(memFoot);
    memRow->addLayout(memCol, 1);
    root->addWidget(memCard, 2);

    // --- tiles: Disks and Network always; Energy, GPU, NPU and Thermals only on hardware that reports them
    tileGrid_ = new QGridLayout;
    tileGrid_->setSpacing(10);
    for (int col = 0; col < 3; ++col) tileGrid_->setColumnStretch(col, 1);
    root->addLayout(tileGrid_);
    tiles_ = {{"disk", diskTile_, vm::Subsystem::Disks, 0}, {"net", netTile_, vm::Subsystem::Network, 0}};
    connectTile(diskTile_, vm::Subsystem::Disks, 0);
    connectTile(netTile_, vm::Subsystem::Network, 0);
    for (std::size_t i = 0; i < tiles_.size(); ++i) tileGrid_->addWidget(tiles_[i].card, static_cast<int>(i) / 3, static_cast<int>(i) % 3);

    table_->setRowCount(0);
    setTheme(theme_, bloom_);
}

void SummaryPage::connectTile(TileCard* card, vm::Subsystem which, int device) {
    connect(card, &TileCard::clicked, this, [this, which, device] {
        emit subsystemRequested(which, device);
        emit pageRequested(Page::Performance);
    });
}

// Keeps the tile set equal to the hardware that reports: rebuilt only when that set changes.
void SummaryPage::syncTiles(const Frame& f) {
    struct Want {
        std::string key;
        vm::Subsystem which;
        int device;
        QString title;
        vm::MeterKind kind;
    };
    std::vector<Want> want = {{"disk", vm::Subsystem::Disks, 0, "Disks", vm::MeterKind::Disk},
                              {"net", vm::Subsystem::Network, 0, "Network", vm::MeterKind::Network}};
    if (f.energy.watts.availability != Availability::Unsupported || f.energy.battery.present) {
        want.push_back({"energy", vm::Subsystem::Energy, 0, "Energy", vm::MeterKind::Energy});
    }
    for (const auto& g : f.gpus) {
        want.push_back({"gpu" + std::to_string(g.index), vm::Subsystem::Gpu, g.index, QString("GPU %1").arg(g.index), vm::MeterKind::Gpu});
    }
    for (const auto& n : f.npus) {
        want.push_back({"npu" + std::to_string(n.index), vm::Subsystem::Npu, n.index, QString("NPU %1").arg(n.index), vm::MeterKind::Gpu});
    }
    if (f.thermals.hotspot.availability != Availability::Unsupported) {
        want.push_back({"thermals", vm::Subsystem::Thermals, 0, "Thermals", vm::MeterKind::Thermal});
    }

    bool same = want.size() == tiles_.size();
    for (std::size_t i = 0; same && i < want.size(); ++i) same = want[i].key == tiles_[i].key;
    if (same) return;

    std::vector<TileEntry> next;
    for (const auto& w : want) {
        TileCard* card = nullptr;
        for (const auto& old : tiles_) {
            if (old.key == w.key) card = old.card;
        }
        if (!card) {
            card = new TileCard(w.title, w.kind, this);
            card->setTheme(theme_, bloom_);
            connectTile(card, w.which, w.device);
            card->show();
        }
        next.push_back({w.key, card, w.which, w.device});
    }
    while (QLayoutItem* item = tileGrid_->takeAt(0)) delete item;  // the widgets themselves are kept or deleted below
    for (const auto& old : tiles_) {
        bool kept = false;
        for (const auto& n : next) kept = kept || n.card == old.card;
        if (!kept) delete old.card;
    }
    tiles_ = std::move(next);
    for (std::size_t i = 0; i < tiles_.size(); ++i) tileGrid_->addWidget(tiles_[i].card, static_cast<int>(i) / 3, static_cast<int>(i) % 3);
}

void SummaryPage::applyCpuSeries() {
    cpuGraph_->clearSeries();
    if (!history_) return;
    if (legend_[0]->isChecked()) cpuGraph_->addSeries(&history_->series(Series::CpuTotal), theme_.cpu, "Utilization", Unit::Percent);
    if (legend_[1]->isChecked()) cpuGraph_->addSeries(&history_->series(Series::CpuKernel), theme_.cpuKernel, "Kernel", Unit::Percent);
    if (legend_[2]->isChecked()) cpuGraph_->addSeries(&history_->series(Series::HotspotC), theme_.temperature, "Temperature", Unit::Celsius);
}

void SummaryPage::update(const Frame& f, const HistoryStore& history, MonoTime now) {
    history_ = &history;
    now_ = now;
    culprits_->setLogicalCpus(static_cast<int>(f.cpu.logical.size()));

    // VFD stack
    const auto vfd = vm::vfdStack(f, clockFullScale_);
    const Metric stackMetrics[4] = {f.cpu.total, f.cpu.freqHz, f.thermals.hotspot,
                                    f.gpus.empty() ? Metric::notMeasured(Availability::Unsupported, Unit::Percent)
                                                   : f.gpus.front().util};
    const double scales[4] = {100, clockFullScale_, 110, 100};
    for (std::size_t i = 0; i < 4; ++i) {
        vfdBars_[i]->setMetric(stackMetrics[i], scales[i]);
        vfdValues_[i]->setText(q(vfd[i].value));
        // SUM-09: hardware with no provider is hidden, not shown as an empty meter.
        vfdColumns_[i]->setVisible(stackMetrics[i].availability != Availability::Unsupported);
    }
    int visibleColumns = 0;
    for (auto* c : vfdColumns_) visibleColumns += c->isHidden() ? 0 : 1;
    vfdCard_->setFixedWidth(24 + std::max(1, visibleColumns) * 76);

    // CPU overview
    cpuValue_->setText(q(vfd[0].value));
    cpuFooterLeft_->setText(f.cpu.logical.empty() ? QStringLiteral("—")
                                                  : QString("%1 logical processors").arg(f.cpu.logical.size()));
    cpuFooterRight_->setText("Speed " + q(format(f.cpu.freqHz)));
    cpuGraph_->setNow(now);
    applyCpuSeries();

    // top processes
    const auto rows = vm::topProcesses(f, static_cast<std::size_t>(topRows_));
    topKeys_.clear();
    for (const auto& row : rows) topKeys_.push_back(row.key);
    totalProcesses_->setText(countText(f.processes.count) + " total");
    table_->setRowCount(static_cast<int>(rows.size()));
    const QFontMetrics fm(table_->font());
    const int nameWidth = std::max(20, table_->columnWidth(kNameColumn) - 14);
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const auto& row = rows[static_cast<std::size_t>(r)];
        const QString cells[5] = {QString::number(row.pid),
                                  fm.elidedText(q(row.name), Qt::ElideRight, nameWidth), q(row.cpu), q(row.gpu),
                                  q(row.memory)};
        for (int c = 0; c < 5; ++c) {
            auto* item = table_->item(r, c);
            if (!item) {
                item = new QTableWidgetItem;
                item->setFlags(Qt::ItemIsEnabled);
                item->setTextAlignment(c == 1 ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
                table_->setItem(r, c, item);
            }
            item->setText(cells[c]);
            if (c == 1) item->setToolTip(plainTip(q(row.name)));
        }
    }

    // memory
    const auto& m = f.memory;
    double pct = 0;
    Metric pctMetric = Metric::notMeasured(Availability::Pending, Unit::Percent);
    if (m.inUse.isMeasured() && m.total.isMeasured() && *m.total.value > 0) {
        pct = *m.inUse.value / *m.total.value * 100.0;
        pctMetric = Metric::measured(pct, Unit::Percent);
    }
    memBar_->setMetric(pctMetric, 100);
    memPct_->setText(q(format(pctMetric)));
    memHeader_->setText(q(vm::memoryHeader(f)));
    memGraph_->clearSeries();
    memGraph_->addSeries(&history.series(Series::MemoryUsedBytes), theme_.memory, "In use", Unit::Bytes);
    if (m.total.isMeasured()) memGraph_->setFixedScale({0, *m.total.value});
    else memGraph_->setAutoScale(1.0);
    memGraph_->setNow(now);
    memAvailable_->setText("Available " + q(format(m.available)));
    memCached_->setText("Cached " + q(format(m.cached)));
    memSwap_->setText("Swap " + q(format(m.swapUsed)));

    // tiles
    syncTiles(f);
    const auto powerScale = vm::autoScale(history.series(Series::PowerW), now, std::chrono::seconds(windowSeconds_), 25.0);
    const auto rx = vm::autoScale(history.series(Series::NetRxBps), now, std::chrono::seconds(windowSeconds_), 512.0 * 1024);
    const auto tx = vm::autoScale(history.series(Series::NetTxBps), now, std::chrono::seconds(windowSeconds_), 512.0 * 1024);
    const Metric netTotal = f.net.combined.rxBps.isMeasured() && f.net.combined.txBps.isMeasured()
                                ? Metric::measured(*f.net.combined.rxBps.value + *f.net.combined.txBps.value, Unit::BytesPerSec)
                                : Metric::notMeasured(Availability::Pending, Unit::BytesPerSec);
    for (const auto& t : tiles_) {
        switch (t.which) {
            case vm::Subsystem::Disks: t.card->set(vm::diskTile(f), f.disks.combined.activePct, 100); break;
            case vm::Subsystem::Network: t.card->set(vm::networkTile(f), netTotal, rx.hi + tx.hi); break;
            case vm::Subsystem::Energy: t.card->set(vm::energyTile(f), f.energy.watts, powerScale.hi); break;
            case vm::Subsystem::Thermals: t.card->set(vm::thermalsTile(f), f.thermals.hotspot, 110); break;
            case vm::Subsystem::Gpu:
                if (static_cast<std::size_t>(t.device) < f.gpus.size()) {
                    t.card->set(vm::gpuTile(f.gpus[static_cast<std::size_t>(t.device)]), f.gpus[static_cast<std::size_t>(t.device)].util, 100);
                }
                break;
            case vm::Subsystem::Npu:
                if (static_cast<std::size_t>(t.device) < f.npus.size()) {
                    t.card->set(vm::npuTile(f.npus[static_cast<std::size_t>(t.device)]), f.npus[static_cast<std::size_t>(t.device)].activePct, 100);
                }
                break;
            default: break;
        }
    }

}

void SummaryPage::setAnimationRate(int hz) {
    const double q = vm::scrollQuantumPx(hz);
    cpuGraph_->setScrollQuantum(q);
    memGraph_->setScrollQuantum(q);
}

void SummaryPage::tick(double dt) {
    const auto now = graphNow();  // the sampler's clock live, the playhead in a replay: graphs scroll smoothly
    cpuGraph_->setNow(now);
    memGraph_->setNow(now);
    for (auto* b : vfdBars_) b->tick(dt);
    memBar_->tick(dt);
    for (const auto& t : tiles_) t.card->tick(dt);
}

void SummaryPage::setTheme(const vm::Theme& theme, bool bloom) {
    theme_ = theme;
    bloom_ = bloom;
    for (auto* b : vfdBars_) {
        b->setTheme(theme);
        b->setBloom(bloom);
    }
    memBar_->setTheme(theme);
    memBar_->setBloom(bloom);
    cpuGraph_->setTheme(theme);
    cpuGraph_->setBloom(bloom);
    memGraph_->setTheme(theme);
    memGraph_->setBloom(bloom);
    for (const auto& t : tiles_) t.card->setTheme(theme, bloom);

    const auto styleLegend = [&](QPushButton* b, vm::Rgb c) {
        const QString hex = toQColor(c).name();
        b->setStyleSheet(QString("QPushButton{color:%1;background:transparent;border:1px solid transparent;"
                                 "border-radius:6px;padding:2px 8px;}"
                                 "QPushButton:checked{border-color:%1;}"
                                 "QPushButton:!checked{color:%2;}")
                             .arg(hex, toQColor(theme.textDim).name()));
    };
    styleLegend(legend_[0], theme.cpu);
    styleLegend(legend_[1], theme.cpuKernel);
    styleLegend(legend_[2], theme.temperature);
    applyCpuSeries();
}

}  // namespace pmp::gui

namespace pmp::gui {

void SummaryPage::setHistoryWindow(int seconds) {
    windowSeconds_ = seconds;
    for (auto* g : findChildren<HistoryGraph*>()) g->setWindow(std::chrono::seconds(seconds));
}

void SummaryPage::setTopProcessCount(int count) {
    topRows_ = count;
    procTitle_->setText(QString("Top CPU processes (%1)").arg(count));
    table_->setMinimumHeight(24 + count * 22 + 4);
}

}  // namespace pmp::gui
