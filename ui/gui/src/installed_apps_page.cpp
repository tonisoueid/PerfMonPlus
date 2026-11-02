// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/installed_apps_page.hpp>

#include <perfmonplus/gui/async.hpp>

#include <perfmonplus/core/format.hpp>

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace pmp::gui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

const QColor kSegmentColours[] = {QColor(0x3d, 0x5c, 0xd6), QColor(0x2e, 0xb8, 0x6b), QColor(0xe0, 0x9a, 0x2b), QColor(0xb0, 0x4a, 0xc8), QColor(0xd0, 0x4a, 0x4a)};

}  // namespace

void AppsTableModel::setRows(std::vector<vm::AppRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

void AppsTableModel::setLoading(bool loading) {
    if (loading_ == loading) return;
    loading_ = loading;
    if (!rows_.empty()) emit dataChanged(index(0, 4), index(rowCount() - 1, 4));
}

QVariant AppsTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const auto& r = rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return q(r.name);
            case 1: return q(r.version);
            case 2: return q(r.publisher);
            case 3: return q(r.installed);
            case 4: return loading_ ? QStringLiteral("Calculating…") : q(r.size);
            case 5: return q(r.lastUsed);
            case 6: return q(r.location);
            case 7: return q(r.source);
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() == 4) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ToolTipRole && index.column() == 5) return QStringLiteral("Linux does not record when an app was last used.");
    if (role == Qt::ToolTipRole && index.column() == 3 && r.sourceKind == linux::AppSource::Dpkg && !r.installedExact) {
        return QStringLiteral("An estimate: dpkg's log no longer has this package, so this is the date its file list was last written.");
    }
    return {};
}

QVariant AppsTableModel::headerData(int section, Qt::Orientation o, int role) const {
    static const char* names[] = {"Name", "Version", "Publisher", "Installed", "Size on disk", "Last used", "Location", "Source"};
    if (o == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < 8) return QString::fromUtf8(names[section]);
    return {};
}

const char* AppsTableModel::sortKey(int column) {
    static const char* keys[] = {"name", "version", "publisher", "installed", "size", "installed", "location", "source"};
    return column >= 0 && column < 8 ? keys[column] : "size";
}

void SizeBar::setData(std::vector<std::pair<QString, double>> parts) {
    parts_ = std::move(parts);
    update();
}

void SizeBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double total = 0;
    for (const auto& [name, bytes] : parts_) total += bytes;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2a, 0x2f, 0x3a));
    p.drawRoundedRect(rect(), 4, 4);
    if (total <= 0) return;
    double x = 0;
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        const double w = parts_[i].second / total * width();
        p.setBrush(kSegmentColours[i % 5]);
        p.drawRect(QRectF(x, 0, std::max(1.0, w), height()));
        x += w;
    }
}

InstalledAppsPage::InstalledAppsPage(CollectFn collect, linux::CommandRunner run, ModulesFn modules, QWidget* parent)
    : QWidget(parent), collect_(std::move(collect)), run_(std::move(run)), modulesFn_(std::move(modules)) {
    hooks_.confirm = [this](const QString& title, const QString& text) {
        QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this);
        box.setTextFormat(Qt::PlainText);  // the app name comes from outside
        auto* yes = box.addButton("Uninstall", QMessageBox::AcceptRole);
        auto* no = box.addButton("Cancel", QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.exec();
        return box.clickedButton() == yes;
    };
    hooks_.showCommand = [this](const QString& command) {
        QMessageBox box(QMessageBox::Information, "Uninstall from a terminal",
                        "PerfMonPlus never runs as administrator. Run this in a terminal:\n\n" + command, QMessageBox::NoButton, this);
        box.setTextFormat(Qt::PlainText);
        auto* copyBtn = box.addButton("Copy command", QMessageBox::ActionRole);
        box.addButton("Close", QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == copyBtn) QApplication::clipboard()->setText(command);
    };
    hooks_.reveal = [this](const QString& path) {
        runAsync<int>(
            this, [run = run_, path] { return run(std::vector<std::string>{"xdg-open", path.toStdString()}).exitCode; }, [](int) {});
    };

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    tabs_ = new QTabWidget(this);
    col->addWidget(tabs_);

    // ---- Apps ----
    auto* appsTab = new QWidget(tabs_);
    auto* ac = new QVBoxLayout(appsTab);
    total_ = new QLabel(appsTab);
    total_->setTextFormat(Qt::PlainText);
    bar_ = new SizeBar(appsTab);
    note_ = new QLabel(appsTab);
    note_->setObjectName("dim");
    note_->setTextFormat(Qt::PlainText);
    ac->addWidget(total_);
    ac->addWidget(bar_);
    ac->addWidget(note_);
    auto* bar = new QHBoxLayout;
    filter_ = new QLineEdit(appsTab);
    filter_->setPlaceholderText("Filter by name, publisher or source");
    filter_->setClearButtonEnabled(true);
    source_ = new QComboBox(appsTab);
    source_->addItem("All sources", -1);
    for (auto s : {linux::AppSource::Dpkg, linux::AppSource::Rpm, linux::AppSource::Flatpak, linux::AppSource::Snap, linux::AppSource::AppImage}) {
        source_->addItem(q(linux::sourceText(s)), static_cast<int>(s));
    }
    uninstall_ = new QPushButton("Uninstall…", appsTab);
    reveal_ = new QPushButton("Reveal", appsTab);
    copy_ = new QPushButton("Copy", appsTab);
    cancelBtn_ = new QPushButton("Cancel", appsTab);
    cancelBtn_->hide();
    bar->addWidget(filter_, 1);
    bar->addWidget(source_);
    bar->addWidget(uninstall_);
    bar->addWidget(reveal_);
    bar->addWidget(copy_);
    bar->addWidget(cancelBtn_);
    ac->addLayout(bar);
    model_ = new AppsTableModel(this);
    table_ = new QTableView(appsTab);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setSortIndicator(4, Qt::DescendingOrder);
    table_->setAccessibleName("Installed apps");
    table_->setColumnWidth(0, 220);
    table_->setColumnWidth(2, 220);
    ac->addWidget(table_, 1);
    status_ = new QLabel(appsTab);
    status_->setObjectName("dim");
    status_->setTextFormat(Qt::PlainText);
    ac->addWidget(status_);
    tabs_->addTab(appsTab, "Apps");

    // ---- Kernel modules ----
    auto* modTab = new QWidget(tabs_);
    auto* mc = new QVBoxLayout(modTab);
    modulesFilter_ = new QLineEdit(modTab);
    modulesFilter_->setPlaceholderText("Filter by module name");
    modulesFilter_->setClearButtonEnabled(true);
    modules_ = new QTableWidget(0, 4, modTab);
    modules_->setHorizontalHeaderLabels({"Name", "Size", "Used by", "State"});
    modules_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modules_->setSelectionBehavior(QAbstractItemView::SelectRows);
    modules_->verticalHeader()->hide();
    modules_->horizontalHeader()->setStretchLastSection(true);
    modules_->horizontalHeader()->setSectionsClickable(true);
    modules_->horizontalHeader()->setSortIndicatorShown(true);
    modules_->horizontalHeader()->setSortIndicator(1, Qt::DescendingOrder);  // biggest first
    modules_->setAccessibleName("Kernel modules");
    mc->addWidget(modulesFilter_);
    mc->addWidget(modules_, 1);
    tabs_->addTab(modTab, "Kernel modules");

    connect(filter_, &QLineEdit::textChanged, this, &InstalledAppsPage::rebuild);
    connect(source_, &QComboBox::currentIndexChanged, this, &InstalledAppsPage::rebuild);
    connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        descending_ = order == Qt::DescendingOrder;
        rebuild();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateButtons(); });
    connect(uninstall_, &QPushButton::clicked, this, &InstalledAppsPage::uninstallSelected);
    connect(reveal_, &QPushButton::clicked, this, &InstalledAppsPage::revealSelected);
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(copyText()); });
    connect(cancelBtn_, &QPushButton::clicked, this, &InstalledAppsPage::cancel);
    connect(modulesFilter_, &QLineEdit::textChanged, this, &InstalledAppsPage::fillModules);
    connect(modules_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](int, Qt::SortOrder) { fillModules(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 1 && mods_.empty()) loadModules();
    });
    updateButtons();
}

void InstalledAppsPage::setHooks(Hooks h) {
    if (h.confirm) hooks_.confirm = std::move(h.confirm);
    if (h.showCommand) hooks_.showCommand = std::move(h.showCommand);
    if (h.reveal) hooks_.reveal = std::move(h.reveal);
}

void InstalledAppsPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!loadedOnce_ && !loading_) refresh();  // nothing is read until the page is first shown
}

void InstalledAppsPage::refresh() {
    if (loading_) return;
    loading_ = true;
    cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    model_->setLoading(true);
    cancelBtn_->show();
    status_->setText("Reading installed software…");
    struct Result {
        std::vector<linux::InstalledApp> apps;
        std::vector<std::string> skipped;
        bool cancelled = false;
    };
    runAsync<Result>(
        this,
        [collect = collect_, flag = cancelFlag_] {
            Result r;
            r.apps = collect(*flag, &r.skipped);
            r.cancelled = flag->load();
            return r;
        },
        [this](Result r) {
            apply(std::move(r.apps), std::move(r.skipped));
            if (r.cancelled) status_->setText(status_->text() + " (stopped early: the list is partial)");
        });
}

void InstalledAppsPage::refreshSync() {
    std::atomic<bool> none{false};
    std::vector<std::string> skipped;
    auto apps = collect_(none, &skipped);
    apply(std::move(apps), std::move(skipped));
}

void InstalledAppsPage::cancel() {
    if (loading_) cancelFlag_->store(true);
}

void InstalledAppsPage::apply(std::vector<linux::InstalledApp> apps, std::vector<std::string> skipped) {
    apps_ = std::move(apps);
    loading_ = false;
    loadedOnce_ = true;
    model_->setLoading(false);
    cancelBtn_->hide();
    QString text = QString("%1 apps").arg(apps_.size());
    if (!skipped.empty()) {
        QStringList notes;
        for (const auto& s : skipped) notes << q(s);
        text += " · not available: " + notes.join("; ");
    }
    status_->setText(text);
    rebuild();
}

void InstalledAppsPage::rebuild() {
    const auto summary = vm::summariseApps(apps_);
    total_->setText(q(summary.total));
    note_->setText(q(summary.note));
    std::vector<std::pair<QString, double>> parts;
    for (const auto& [src, bytes] : summary.bySource) parts.emplace_back(q(linux::sourceText(src)), bytes);
    bar_->setData(std::move(parts));
    QString legend;
    for (const auto& [src, bytes] : summary.bySource) {
        legend += (legend.isEmpty() ? "" : "   ") + q(linux::sourceText(src)) + " " + q(pmp::format(Metric::measured(bytes, Unit::Bytes)));
    }
    bar_->setToolTip(legend);
    bar_->setAccessibleDescription(legend);

    vm::AppsOptions o;
    o.filter = filter_->text().toStdString();
    if (source_->currentData().toInt() >= 0) o.source = static_cast<linux::AppSource>(source_->currentData().toInt());
    o.sortBy = AppsTableModel::sortKey(sortColumn_);
    o.descending = descending_;
    QString keep;
    if (const auto rows = table_->selectionModel()->selectedRows(); !rows.isEmpty()) keep = q(model_->rowAt(rows.first().row()).key);
    model_->setRows(vm::buildApps(apps_, o));
    if (!keep.isEmpty()) {
        for (int i = 0; i < model_->rowCount(); ++i) {
            if (q(model_->rowAt(i).key) == keep) {
                table_->selectRow(i);
                break;
            }
        }
    }
    updateButtons();
}

const linux::InstalledApp* InstalledAppsPage::selectedApp() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return nullptr;
    const auto key = model_->rowAt(rows.first().row()).key;
    for (const auto& a : apps_) {
        if (linux::sourceText(a.source) + "|" + a.id == key) return &a;
    }
    return nullptr;
}

void InstalledAppsPage::updateButtons() {
    const auto* a = selectedApp();
    uninstall_->setEnabled(a != nullptr);
    reveal_->setEnabled(a != nullptr && (!a->location.empty() || (a->source == linux::AppSource::Dpkg && locator_)));
    copy_->setEnabled(a != nullptr);
}

QString InstalledAppsPage::copyText() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return {};
    const auto& r = model_->rowAt(rows.first().row());
    return QStringList{q(r.name), q(r.version), q(r.publisher), q(r.installed), q(r.size), q(r.location), q(r.source)}.join('\t');
}

void InstalledAppsPage::uninstallSelected() {
    const auto* app = selectedApp();
    if (!app) return;
    const auto copy = *app;  // the list may refresh while a dialog is open
    if (const auto cmd = linux::InstalledAppsCollector::userUninstallCommand(copy)) {
        if (!hooks_.confirm("Uninstall " + q(copy.name) + "?",
                            "Remove " + q(copy.name) + " for your user? The app is removed; its data folder is kept.")) {
            return;
        }
        status_->setText("Removing " + q(copy.name) + "…");
        runAsync<linux::CommandResult>(
            this, [run = run_, argv = *cmd] { return run(argv); },
            [this, name = q(copy.name)](linux::CommandResult r) {
                status_->setText(r.exitCode == 0 ? name + " was removed. Refresh to update the list."
                                                  : "Could not remove " + name + ": " + q(linux::describeFailure(r)));
            });
        return;
    }
    const auto hint = linux::InstalledAppsCollector::uninstallHint(copy);
    if (hint.empty()) {
        status_->setText(q(copy.name) + " is a single file: use Reveal and delete it yourself if you no longer want it.");
        return;
    }
    hooks_.showCommand(q(hint));
}

void InstalledAppsPage::revealSelected() {
    const auto* a = selectedApp();
    if (!a) return;
    std::string where = a->location;
    if (where.empty() && locator_) where = locator_(*a);
    if (where.empty()) {
        status_->setText(q(a->name) + " has no folder of its own: its files are spread across the system.");
        return;
    }
    QString path = q(where);
    if (QFileInfo(path).isFile()) path = QFileInfo(path).absolutePath();
    hooks_.reveal(path);
}

void InstalledAppsPage::loadModules() {
    runAsync<std::vector<linux::KernelModule>>(
        this, [fn = modulesFn_] { return fn ? fn() : std::vector<linux::KernelModule>{}; },
        [this](std::vector<linux::KernelModule> m) {
            mods_ = std::move(m);
            fillModules();
        });
}

void InstalledAppsPage::fillModules() {
    const int col = modules_->horizontalHeader()->sortIndicatorSection();
    const char* keys[] = {"name", "size", "usedby", "state"};
    const bool desc = modules_->horizontalHeader()->sortIndicatorOrder() == Qt::DescendingOrder;
    const auto rows = vm::buildModules(mods_, modulesFilter_->text().toStdString(), keys[std::clamp(col, 0, 3)], desc);
    modules_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        modules_->setItem(i, 0, new QTableWidgetItem(q(r.name)));
        auto* size = new QTableWidgetItem(q(r.size));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        modules_->setItem(i, 1, size);
        modules_->setItem(i, 2, new QTableWidgetItem(q(r.usedBy)));
        modules_->setItem(i, 3, new QTableWidgetItem(q(r.state)));
    }
}

}  // namespace pmp::gui
