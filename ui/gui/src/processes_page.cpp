// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/processes_page.hpp>

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <perfmonplus/vm/process_details.hpp>

#include <algorithm>
#include <functional>
#include <map>

namespace pmp::gui {
namespace {

constexpr int kIndent = 16;       // pixels per depth level in the Name column
constexpr int kChevronSlot = 16;  // room for the expand/collapse chevron

QColor qc(vm::Rgb c, int alpha = 255) { return QColor(c.r, c.g, c.b, alpha); }

// Draws a row: heat bar, chevron and indent in the Name column, red tint for tombstones, bold header rows.
// Everything is drawn as plain text, so a process called "<b>x</b>" shows exactly that.
class ProcessDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    vm::Theme theme = vm::darkTheme();

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override {
        const auto type = static_cast<vm::TableRow::Type>(index.data(ProcessTableModel::TypeRole).toInt());
        const bool selected = opt.state & QStyle::State_Selected;
        p->save();
        if (type == vm::TableRow::Type::Header) {
            p->fillRect(opt.rect, qc(theme.panel));
            QFont f = opt.font;
            f.setBold(true);
            p->setFont(f);
            p->setPen(qc(theme.text));
            const int depth = 0;
            const bool expanded = index.data(ProcessTableModel::ExpandedRole).toBool();
            p->drawText(opt.rect.adjusted(6 + depth, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                        QString(expanded ? "▾  " : "▸  ") + index.data(Qt::DisplayRole).toString());
            p->restore();
            return;
        }
        const bool tomb = index.data(ProcessTableModel::TombstoneRole).toBool();
        if (selected) p->fillRect(opt.rect, qc(theme.accent, 70));
        else if (tomb) p->fillRect(opt.rect, QColor(220, 60, 60, 45));

        const double heat = index.data(ProcessTableModel::HeatRole).toDouble();
        if (heat > 0) {
            QRect bar = opt.rect.adjusted(1, 3, -1, -3);
            bar.setWidth(std::max(1, static_cast<int>(bar.width() * heat)));
            p->fillRect(bar, qc(theme.accent, 80));
        }

        QRect text = opt.rect.adjusted(6, 0, -6, 0);
        if (index.column() == 0) {
            const int depth = index.data(ProcessTableModel::DepthRole).toInt();
            const int x = text.left() + depth * kIndent;
            if (index.data(ProcessTableModel::ExpandableRole).toBool()) {
                p->setPen(qc(theme.textDim));
                p->drawText(QRect(x, text.top(), kChevronSlot, text.height()), Qt::AlignVCenter | Qt::AlignLeft,
                            index.data(ProcessTableModel::ExpandedRole).toBool() ? "▾" : "▸");
            }
            text.setLeft(x + kChevronSlot);
        }
        p->setPen(tomb ? QColor(235, 110, 110) : qc(theme.text));
        const auto align = static_cast<Qt::Alignment>(index.data(Qt::TextAlignmentRole).toInt());
        const QString shown = opt.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width());
        p->drawText(text, align, shown);
        p->restore();
    }
};

}  // namespace

// A QTableView that knows about expansion: clicking a chevron or a header row toggles it, and Left/Right
// collapse and expand the current row.
class ProcessTableView : public QTableView {
public:
    using QTableView::QTableView;
    std::function<void(int row)> toggle;
    std::function<void()> onDelete;

protected:
    void mousePressEvent(QMouseEvent* e) override {
        const QModelIndex index = indexAt(e->pos());
        if (index.isValid() && e->button() == Qt::LeftButton && toggle) {
            const auto type = static_cast<vm::TableRow::Type>(index.data(ProcessTableModel::TypeRole).toInt());
            if (type == vm::TableRow::Type::Header) {
                toggle(index.row());
                return;
            }
            if (index.column() == 0 && index.data(ProcessTableModel::ExpandableRole).toBool()) {
                const int x = visualRect(index).left() + 6 + index.data(ProcessTableModel::DepthRole).toInt() * kIndent;
                if (e->pos().x() >= x - 2 && e->pos().x() <= x + kChevronSlot) {
                    toggle(index.row());
                    return;
                }
            }
        }
        QTableView::mousePressEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Delete && onDelete) {
            onDelete();
            return;
        }
        const QModelIndex cur = currentIndex();
        if (cur.isValid() && toggle && (e->key() == Qt::Key_Right || e->key() == Qt::Key_Left)) {
            const bool expandable = cur.data(ProcessTableModel::ExpandableRole).toBool();
            const bool expanded = cur.data(ProcessTableModel::ExpandedRole).toBool();
            if (expandable && (e->key() == Qt::Key_Right) != expanded) {
                toggle(cur.row());
                return;
            }
        }
        QTableView::keyPressEvent(e);
    }
};

ProcessesPage::ProcessesPage(QWidget* parent) : ProcessesPage(std::make_shared<linux::ProcessInspector>(), parent) {}

ProcessesPage::ProcessesPage(std::shared_ptr<const linux::ProcessInspector> inspector, QWidget* parent)
    : QWidget(parent),
      model_(new ProcessTableModel(this)),
      table_(new ProcessTableView(this)),
      view_(new QComboBox(this)),
      filter_(new QLineEdit(this)),
      message_(new QLabel(this)),
      split_(new QSplitter(Qt::Vertical, this)),
      details_(new ProcessDetailsPane(std::move(inspector), this)),
      actions_(new ProcessActionsController(makeRealActionPort(), this)),
      forceBar_(new QFrame(this)),
      forceLabel_(new QLabel(this)),
      forceBtn_(new QPushButton("Force quit", this)),
      endBtn_(new QToolButton(this)),
      suspendBtn_(new QToolButton(this)),
      resumeBtn_(new QToolButton(this)),
      columns_(vm::defaultColumns()) {
    view_->addItem("Process View", static_cast<int>(vm::ProcessView::Grouped));
    view_->addItem("Tree View", static_cast<int>(vm::ProcessView::Tree));
    view_->addItem("Flat List", static_cast<int>(vm::ProcessView::Flat));
    view_->addItem("By User", static_cast<int>(vm::ProcessView::ByUser));
    filter_->setPlaceholderText("Filter by name, user, or PID");
    filter_->setClearButtonEnabled(true);
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    message_->hide();

    table_->setModel(model_);
    table_->setItemDelegate(new ProcessDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->toggle = [this](int row) { toggleExpanded(row); };

    endBtn_->setText("End task");
    suspendBtn_->setText("Suspend");
    resumeBtn_->setText("Resume");
    for (auto* b : {endBtn_, suspendBtn_, resumeBtn_}) {
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setEnabled(false);
    }
    forceLabel_->setTextFormat(Qt::PlainText);
    auto* forceRow = new QHBoxLayout(forceBar_);
    forceRow->setContentsMargins(10, 4, 10, 4);
    forceRow->addWidget(forceLabel_);
    forceRow->addStretch(1);
    forceRow->addWidget(forceBtn_);
    forceBar_->hide();

    auto* bar = new QHBoxLayout;
    bar->addWidget(endBtn_);
    bar->addWidget(suspendBtn_);
    bar->addWidget(resumeBtn_);
    bar->addStretch(1);
    bar->addWidget(view_);
    bar->addWidget(filter_, 1);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    col->addLayout(bar);
    col->addWidget(message_);
    col->addWidget(forceBar_);
    split_->addWidget(table_);
    split_->addWidget(details_);
    split_->setStretchFactor(0, 3);
    split_->setStretchFactor(1, 1);
    split_->setChildrenCollapsible(true);
    col->addWidget(split_, 1);
    details_->hide();  // nothing is selected yet

    connect(view_, &QComboBox::currentIndexChanged, this, [this](int i) {
        mode_ = static_cast<vm::ProcessView>(view_->itemData(i).toInt());
        rebuild();
        if (rememberView_) {
            savedViewIndex_ = i;
            emit settingsChanged();
        }
    });
    connect(filter_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section >= 0 && section < static_cast<int>(columns_.size())) sortBy(columns_[static_cast<std::size_t>(section)]);
    });
    connect(table_->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            [this](const QPoint& p) { showColumnMenu(table_->horizontalHeader()->mapToGlobal(p)); });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->onDelete = [this] { endSelectedTask(true); };
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) { showContextMenu(table_->viewport()->mapToGlobal(p)); });
    connect(endBtn_, &QToolButton::clicked, this, [this] { endSelectedTask(false); });
    connect(suspendBtn_, &QToolButton::clicked, this, [this] { if (auto r = selectedRecord()) actions_->suspend(*r); });
    connect(resumeBtn_, &QToolButton::clicked, this, [this] { if (auto r = selectedRecord()) actions_->resume(*r); });
    connect(forceBtn_, &QPushButton::clicked, this, [this] {
        forceBar_->hide();
        for (const auto& r : frame_.processes.processes) {
            if (r.key == forceKey_) {
                actions_->forceQuit(r);
                return;
            }
        }
    });
    messageTimer_.setSingleShot(true);
    messageTimer_.setInterval(12000);
    connect(&messageTimer_, &QTimer::timeout, message_, &QLabel::hide);
    connect(actions_, &ProcessActionsController::outcome, this, [this](const QString& text, bool ok) {
        message_->setText(text);
        message_->setProperty("ok", ok);
        message_->show();
        messageTimer_.start();
    });
    connect(actions_, &ProcessActionsController::forceQuitOffered, this, [this](ProcessKey key, const QString& name) {
        forceKey_ = key;
        forceLabel_->setText(QString("%1 did not exit.").arg(name));
        forceBar_->show();
    });
    connect(model_, &ProcessTableModel::structureChanged, this, [this] { applySpans(); });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { onSelectionChanged(); });
    // A double click also reports "activated"; only Enter should toggle the pane (PRC-16).
    connect(table_, &QAbstractItemView::doubleClicked, this, [this] {
        ignoreActivate_ = true;
        QTimer::singleShot(0, this, [this] { ignoreActivate_ = false; });
    });
    connect(table_, &QAbstractItemView::activated, this, [this] {
        if (ignoreActivate_) return;
        setDetailsOpen(!wantDetails_);
        emit settingsChanged();
    });
    applyColumns();
}

// An offer to force-quit made a minute ago is not what you want to see when you come back. (It would still be safe:
// the identity is checked again before anything is sent.)
void ProcessesPage::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    forceBar_->hide();
}

QTableView* ProcessesPage::table() const { return table_; }

void ProcessesPage::setTheme(const vm::Theme& theme, bool bloom) {
    if (auto* d = dynamic_cast<ProcessDelegate*>(table_->itemDelegate())) d->theme = theme;
    details_->setTheme(theme, bloom);
    table_->viewport()->update();
}

vm::ProcessTableOptions ProcessesPage::options() const {
    vm::ProcessTableOptions o;
    o.view = mode_;
    o.filter = filter_->text().toStdString();
    o.sortBy = sortBy_;
    o.descending = descending_;
    o.toggled = toggled_;
    o.tombstoneSeconds = tombstoneSeconds_;
    o.reveal = reveal_;
    o.pinned = pinned_;
    return o;
}

void ProcessesPage::update(const Frame& frame, const HistoryStore&, MonoTime) {
    if (!isVisible()) return;  // nothing to show, so nothing to build
    frame_ = frame;
    haveFrame_ = true;
    rebuild();
}

void ProcessesPage::rebuild() {
    if (!haveFrame_) return;
    vm::CellContext ctx;
    ctx.now = frame_.mono;
    ctx.wallNow = frame_.wall;
    ctx.totalMemoryBytes = frame_.memory.total.isMeasured() ? *frame_.memory.total.value : 0;
    // A selected row that was on screen must stay on screen when a re-sort moves it (PRC-05).
    const QModelIndex before = table_->currentIndex();
    const bool wasVisible = before.isValid() && table_->viewport()->rect().intersects(table_->visualRect(before));
    model_->setRows(vm::buildProcessTable(frame_, options(), ctx));
    ++rebuilds_;
    if (wasVisible && table_->currentIndex().isValid()) table_->scrollTo(table_->currentIndex(), QAbstractItemView::EnsureVisible);
    actions_->frameUpdated(frame_);
    onSelectionChanged();
    if (details_->isVisibleTo(this)) details_->update(frame_, history_);
}

const ProcessRecord* ProcessesPage::find(ProcessKey key) const {
    for (const auto& r : frame_.processes.processes) {
        if (r.key == key) return &r;
    }
    for (const auto& r : pinned_) {  // a pinned tombstone outlives the provider's own copy
        if (r.key == key) return &r;
    }
    return nullptr;
}

void ProcessesPage::updateActionState() {
    const auto r = selectedRecord();
    const bool live = r && !r->exitedAt && !ProcessActionsController::isKernelThread(*r);
    for (auto* b : {endBtn_, suspendBtn_, resumeBtn_}) b->setEnabled(live);
    // A group row shows the whole app's totals but acts on its main process: say so where the action is offered.
    QString hint;
    if (const auto idx = table_->currentIndex(); idx.isValid() && model_->rowAt(idx.row()).type == vm::TableRow::Type::Group) {
        const auto others = static_cast<int>(model_->rowAt(idx.row()).members.size()) - 1;
        hint = QString("This row shows the whole app, but this acts on its main process only; %1 other process%2 keep%3 running. "
                       "Use End process tree to end them all.")
                   .arg(others)
                   .arg(others == 1 ? "" : "es")
                   .arg(others == 1 ? "s" : "");
    }
    for (auto* b : {endBtn_, suspendBtn_, resumeBtn_}) b->setToolTip(hint);
}

std::optional<ProcessRecord> ProcessesPage::selectedRecord() const {
    const auto key = selectedKey();
    if (!key) return std::nullopt;
    if (const auto* r = find(*key)) return *r;
    return std::nullopt;
}

void ProcessesPage::endSelectedTask(bool viaKeyboard) {
    const auto r = selectedRecord();
    if (!r || r->exitedAt) return;
    actions_->endTask(*r, viaKeyboard);
}

std::vector<ProcessRecord> ProcessesPage::descendantsLeavesFirst(ProcessKey root) const {
    std::map<ProcessKey, std::vector<const ProcessRecord*>> kids;
    for (const auto& r : frame_.processes.processes) {
        if (!r.exitedAt && r.parent) kids[*r.parent].push_back(&r);
    }
    std::vector<ProcessRecord> out;
    std::set<ProcessKey> seen{root};
    const auto walk = [&](auto&& self, ProcessKey k) -> void {
        for (const auto* c : kids[k]) {
            if (!seen.insert(c->key).second) continue;
            self(self, c->key);
            out.push_back(*c);  // children before their parent
        }
    };
    walk(walk, root);
    return out;
}

void ProcessesPage::showMessage(const QString& text) {
    message_->setText(text);
    message_->show();
    messageTimer_.start();
}

void ProcessesPage::pinTombstone(ProcessKey key) {
    for (const auto& p : pinned_) {
        if (p.key == key) return;
    }
    if (const auto* r = find(key); r && r->exitedAt) pinned_.push_back(*r);
    rebuild();
}

void ProcessesPage::unpinTombstone(ProcessKey key) {
    pinned_.erase(std::remove_if(pinned_.begin(), pinned_.end(), [&](const ProcessRecord& r) { return r.key == key; }), pinned_.end());
    rebuild();
}

void ProcessesPage::showContextMenu(const QPoint& at) {
    const QModelIndex idx = table_->indexAt(table_->viewport()->mapFromGlobal(at));
    if (idx.isValid() && model_->rowAt(idx.row()).type != vm::TableRow::Type::Header) {
        table_->setCurrentIndex(model_->index(idx.row(), 0));
        table_->selectRow(idx.row());
    }
    const auto sel = selectedRecord();
    if (!sel) return;
    const ProcessRecord rec = *sel;
    const bool live = !rec.exitedAt;
    const bool kernel = ProcessActionsController::isKernelThread(rec);
    const bool actable = live && !kernel;
    const QString kernelTip = "Kernel threads cannot be ended or suspended.";

    QMenu menu(this);
    menu.setToolTipsVisible(true);  // so a disabled action can say why (kernel threads, other users)
    const auto add = [&](const QString& text, bool enabled, std::function<void()> fn, const QString& tip = {}) {
        auto* a = menu.addAction(text);
        a->setEnabled(enabled);
        if (!enabled && !tip.isEmpty()) a->setToolTip(tip);
        connect(a, &QAction::triggered, this, std::move(fn));
    };
    add("End task", actable, [this, rec] { actions_->endTask(rec, false); }, kernel ? kernelTip : QString());
    add("End process tree", actable, [this, rec] { actions_->endTree(rec, descendantsLeavesFirst(rec.key)); }, kernel ? kernelTip : QString());
    menu.addSeparator();
    add("Suspend", actable && rec.state != 'T', [this, rec] { actions_->suspend(rec); }, kernel ? kernelTip : QString());
    add("Resume", actable && rec.state == 'T', [this, rec] { actions_->resume(rec); }, kernel ? kernelTip : QString());
    menu.addSeparator();
    add("Set priority…", actable, [this, rec] { actions_->choosePriority(rec); });
    add("Set affinity…", actable, [this, rec] { std::vector<int> ids;
        for (const auto& l : frame_.cpu.logical) ids.push_back(l.id);
        actions_->chooseAffinity(rec, ids); });
    menu.addSeparator();
    add("Open file location", !rec.exe.empty(), [this, rec] { actions_->openLocation(rec); });
    auto* copy = menu.addMenu("Copy");
    const auto cells = [&] {
        std::vector<std::string> out;
        for (int c = 0; c < model_->columnCount(); ++c) {
            const auto row = idx.isValid() ? idx.row() : table_->currentIndex().row();
            out.push_back(model_->index(row, c).data().toString().toStdString());
        }
        return out;
    }();
    connect(copy->addAction("Row"), &QAction::triggered, this, [this, cells] { actions_->copyRow(QString::fromStdString(vm::rowCopyText(cells))); });
    connect(copy->addAction("PID"), &QAction::triggered, this, [this, rec] { actions_->copyPid(rec); });
    auto* path = copy->addAction("Path");
    path->setEnabled(!rec.exe.empty());
    connect(path, &QAction::triggered, this, [this, rec] { actions_->copyPath(rec); });
    QString shown = QString::fromStdString(rec.name);
    if (shown.size() > 30) shown = shown.left(29) + "…";
    shown.replace("&", "&&");  // no keyboard accelerators from a process name
    add(QString("Search online for \"%1\"").arg(shown), true, [this, rec] { actions_->searchOnline(rec); });
    if (rec.exitedAt) {
        menu.addSeparator();
        bool pinned = false;
        for (const auto& p : pinned_) pinned = pinned || p.key == rec.key;
        add(pinned ? "Unpin" : "Pin this row", true, [this, rec, pinned] { pinned ? unpinTombstone(rec.key) : pinTombstone(rec.key); });
    }
    menu.exec(at);
}

void ProcessesPage::onSelectionChanged() {
    updateActionState();
    const auto key = selectedKey();
    const ProcessRecord* rec = key ? find(*key) : nullptr;
    if (!rec) {
        if (!key) details_->hide();
        details_->setProcess(nullptr, nullptr);
        return;
    }
    const ProcessRecord* parent = rec->parent ? find(*rec->parent) : nullptr;
    details_->setProcess(rec, parent);
    if (!details_->isVisibleTo(this) && wantDetails_) details_->show();
}

void ProcessesPage::setDetailsOpen(bool open) {
    wantDetails_ = open;
    if (open && selectedKey()) details_->show();
    else details_->hide();
}

void ProcessesPage::setShowEnvironment(bool on) { details_->setShowEnvironment(on); }

void ProcessesPage::applyColumns() {
    model_->setColumns(columns_);
    auto* header = table_->horizontalHeader();
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
        const auto c = columns_[static_cast<std::size_t>(i)];
        table_->setColumnWidth(i, c == vm::ProcessColumn::Name ? 300 : c == vm::ProcessColumn::Pid ? 70 : c == vm::ProcessColumn::CommandLine || c == vm::ProcessColumn::Path ? 260 : 110);
    }
    header->setStretchLastSection(true);
    updateSortIndicator();
}

void ProcessesPage::applySpans() {
    table_->clearSpans();
    const int cols = model_->columnCount();
    if (cols < 2) return;
    for (int r = 0; r < model_->rowCount(); ++r) {
        if (model_->rowAt(r).type == vm::TableRow::Type::Header) table_->setSpan(r, 0, 1, cols);
    }
}

void ProcessesPage::updateSortIndicator() {
    auto* header = table_->horizontalHeader();
    const QSignalBlocker block(header);
    const auto it = std::find(columns_.begin(), columns_.end(), sortBy_);
    if (it == columns_.end()) {
        header->setSortIndicator(-1, Qt::AscendingOrder);
        return;
    }
    header->setSortIndicator(static_cast<int>(it - columns_.begin()), descending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void ProcessesPage::sortBy(vm::ProcessColumn column) {
    if (column == sortBy_) {
        descending_ = !descending_;
    } else {
        sortBy_ = column;
        descending_ = vm::columnInfo(column).numeric;  // biggest first for numbers, A-Z for text
    }
    updateSortIndicator();
    rebuild();
    emit settingsChanged();
}

void ProcessesPage::setFilter(const QString& text) { filter_->setText(text); }

void ProcessesPage::setView(vm::ProcessView v, bool remember) {
    rememberView_ = remember;
    view_->setCurrentIndex(view_->findData(static_cast<int>(v)));
    rememberView_ = true;
}

void ProcessesPage::setTombstoneSeconds(int seconds) {
    if (seconds == tombstoneSeconds_) return;
    tombstoneSeconds_ = seconds;
    rebuild();
}

void ProcessesPage::setColumnVisible(vm::ProcessColumn column, bool visible) {
    if (column == vm::ProcessColumn::Name) return;
    const auto it = std::find(columns_.begin(), columns_.end(), column);
    if ((it != columns_.end()) == visible) return;
    if (visible) {
        // keep columns in their canonical order
        columns_.push_back(column);
        std::sort(columns_.begin(), columns_.end(), [](auto a, auto b) { return static_cast<int>(a) < static_cast<int>(b); });
    } else {
        columns_.erase(it);
        if (sortBy_ == column) {  // the sort column went away: fall back to the default
            sortBy_ = vm::ProcessColumn::Memory;
            descending_ = true;
        }
    }
    const auto keep = selectedKey();  // resetting the model's columns clears the selection
    applyColumns();
    rebuild();
    if (keep) {
        const std::string id = "p:" + std::to_string(keep->pid) + ":" + std::to_string(keep->startTicks);
        if (const int row = model_->rowOfId(id); row >= 0) {
            table_->setCurrentIndex(model_->index(row, 0));
            table_->selectRow(row);
        }
    }
    if (column == vm::ProcessColumn::CommandLine) emit commandLineCollectionRequested(visible);
    emit settingsChanged();
}

void ProcessesPage::showColumnMenu(const QPoint& at) {
    QMenu menu(this);
    for (int i = 0; i < vm::kColumnCount; ++i) {
        const auto c = static_cast<vm::ProcessColumn>(i);
        auto* a = menu.addAction(QString::fromUtf8(vm::columnInfo(c).title));
        a->setCheckable(true);
        a->setChecked(std::find(columns_.begin(), columns_.end(), c) != columns_.end());
        a->setEnabled(c != vm::ProcessColumn::Name);
        connect(a, &QAction::toggled, this, [this, c](bool on) { setColumnVisible(c, on); });
    }
    menu.exec(at);
}

void ProcessesPage::toggleExpanded(int row) {
    if (row < 0 || row >= model_->rowCount()) return;
    const std::string id = model_->rowAt(row).id;
    if (!toggled_.erase(id)) toggled_.insert(id);
    reveal_.reset();
    rebuild();
}

std::optional<ProcessKey> ProcessesPage::selectedKey() const {
    const auto idx = table_->currentIndex();
    if (!idx.isValid() || !table_->selectionModel()->isRowSelected(idx.row(), {})) return std::nullopt;
    const auto& row = model_->rowAt(idx.row());
    if (row.type == vm::TableRow::Type::Header) return std::nullopt;
    return row.key;
}

bool ProcessesPage::isRunning(ProcessKey key) const {
    for (const auto& p : frame_.processes.processes) {
        if (p.key == key && !p.exitedAt) return true;
    }
    return false;
}

bool ProcessesPage::selectProcess(ProcessKey key) {
    if (!haveFrame_) return false;
    const std::string id = "p:" + std::to_string(key.pid) + ":" + std::to_string(key.startTicks);
    reveal_ = key;
    rebuild();
    int row = model_->rowOfId(id);
    if (row >= 0) {
        // Remember what had to be opened, so the next rebuild does not close it again.
        for (int r = 0; r < model_->rowCount(); ++r) {
            const auto& tr = model_->rowAt(r);
            const bool dflt = tr.type == vm::TableRow::Type::Header ? true : tr.type == vm::TableRow::Type::Group ? false : tr.depth == 0;
            if (tr.expandable && tr.expanded != dflt) toggled_.insert(tr.id);
        }
    }
    reveal_.reset();
    rebuild();
    row = model_->rowOfId(id);
    if (row < 0) return false;
    table_->setCurrentIndex(model_->index(row, 0));
    table_->selectRow(row);
    table_->scrollTo(model_->index(row, 0));
    return true;
}

void ProcessesPage::applySettings(const AppSettings& s) {
    {
        const QSignalBlocker block(view_);
        view_->setCurrentIndex(std::clamp(s.processView, 0, 3));
        savedViewIndex_ = view_->currentIndex();
    }
    mode_ = static_cast<vm::ProcessView>(view_->currentData().toInt());
    sortBy_ = vm::columnFromKey(s.processSort).value_or(vm::ProcessColumn::Memory);
    descending_ = s.processSortDescending;
    columns_.clear();
    for (const QString& key : s.processColumns) {
        if (const auto c = vm::columnFromKey(key.toStdString())) columns_.push_back(*c);
    }
    if (columns_.empty() || columns_.front() != vm::ProcessColumn::Name) columns_.insert(columns_.begin(), vm::ProcessColumn::Name);
    tombstoneSeconds_ = s.tombstoneSeconds;
    wantDetails_ = s.detailsPaneOpen;
    details_->setShowEnvironment(s.showEnvironment);
    applyColumns();
    rebuild();
}

void ProcessesPage::exportSettings(AppSettings& s) const {
    s.processView = savedViewIndex_;
    s.processSort = vm::columnInfo(sortBy_).key;
    s.processSortDescending = descending_;
    s.processColumns.clear();
    for (const auto c : columns_) s.processColumns << QString::fromUtf8(vm::columnInfo(c).key);
    s.tombstoneSeconds = tombstoneSeconds_;
    s.detailsPaneOpen = wantDetails_;
}

}  // namespace pmp::gui
