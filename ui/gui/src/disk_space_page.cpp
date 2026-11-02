// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/disk_space_page.hpp>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/vm/disk_space_model.hpp>

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>

#include <algorithm>

namespace pmp::gui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QTableWidget* makeTable(QWidget* parent, const QStringList& headers) {
    auto* t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setShowGrid(false);
    return t;
}

}  // namespace

DiskSpacePage::DiskSpacePage(ScanFn scan, MountsFn mounts, QWidget* parent)
    : QWidget(parent), scanFn_(std::move(scan)), mountsFn_(std::move(mounts)) {
    hooks_.confirm = [this](const QString& title, const QString& text) {
        QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this);
        box.setTextFormat(Qt::PlainText);  // file names come from the disk
        auto* yes = box.addButton("Move to Trash", QMessageBox::AcceptRole);
        auto* no = box.addButton("Cancel", QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.exec();
        return box.clickedButton() == yes;
    };
    hooks_.chooseFolder = [this] { return QFileDialog::getExistingDirectory(this, "Choose a folder to scan"); };
    hooks_.reveal = [this](const QString& dir) {
        runAsync<int>(this, [dir] { return linux::runDefault(std::vector<std::string>{"xdg-open", dir.toStdString()}).exitCode; }, [](int) {});
    };
    hooks_.run = linux::runDefault;

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    auto* bar = new QHBoxLayout;
    volume_ = new QComboBox(this);
    volume_->setAccessibleName("Volume to scan");
    volume_->setMinimumWidth(260);
    choose_ = new QPushButton("Choose folder…", this);
    scan_ = new QPushButton("Scan", this);
    bar->addWidget(volume_, 1);
    bar->addWidget(choose_);
    bar->addWidget(scan_);
    col->addLayout(bar);
    progress_ = new QLabel(this);
    progress_->setObjectName("dim");
    progress_->setTextFormat(Qt::PlainText);
    col->addWidget(progress_);

    tabs_ = new QTabWidget(this);
    auto* mapTab = new QWidget(tabs_);
    auto* mc = new QVBoxLayout(mapTab);
    crumbs_ = new QLabel(mapTab);
    crumbs_->setTextFormat(Qt::PlainText);
    map_ = new TreemapWidget(mapTab);
    mc->addWidget(crumbs_);
    mc->addWidget(map_, 1);
    tabs_->addTab(mapTab, "Map");
    auto* largeTab = new QWidget(tabs_);
    auto* lc = new QVBoxLayout(largeTab);
    filter_ = new QLineEdit(largeTab);
    filter_->setPlaceholderText("Filter by name, folder or type");
    filter_->setClearButtonEnabled(true);
    largest_ = makeTable(largeTab, {"Name", "Size", "Folder", "Modified", "Type"});
    largest_->horizontalHeader()->setSectionsClickable(true);
    largest_->horizontalHeader()->setSortIndicatorShown(true);
    largest_->horizontalHeader()->setSortIndicator(1, Qt::DescendingOrder);
    largest_->setAccessibleName("Largest files");
    lc->addWidget(filter_);
    lc->addWidget(largest_, 1);
    tabs_->addTab(largeTab, "Largest files");
    types_ = makeTable(tabs_, {"Type", "Files", "Size", "Share"});
    types_->setAccessibleName("File types");
    tabs_->addTab(types_, "File types");
    col->addWidget(tabs_, 1);

    summary_ = new QLabel(this);
    summary_->setTextFormat(Qt::PlainText);
    status_ = new QLabel(this);
    status_->setObjectName("dim");
    status_->setTextFormat(Qt::PlainText);
    auto* actions = new QHBoxLayout;
    reveal_ = new QPushButton("Reveal", this);
    trash_ = new QPushButton("Move to Trash…", this);
    copy_ = new QPushButton("Copy path", this);
    actions->addWidget(summary_, 1);
    actions->addWidget(reveal_);
    actions->addWidget(trash_);
    actions->addWidget(copy_);
    col->addLayout(actions);
    col->addWidget(status_);

    // Volumes: real file systems only; pseudo ones (proc, sysfs...) are not worth scanning.
    for (const auto& m : mountsFn_ ? mountsFn_() : std::vector<linux::MountInfo>{}) {
        if (m.pseudo) continue;
        volume_->addItem(QString("%1 — %2, %3 free").arg(q(m.mountPoint), q(m.fsType), q(pmp::format(Metric::measured(static_cast<double>(m.freeBytes), Unit::Bytes)))),
                         q(m.mountPoint));
    }

    connect(scan_, &QPushButton::clicked, this, [this] {
        if (scanning_) cancelScan();
        else if (volume_->currentIndex() >= 0) scanPath(volume_->currentData().toString());
    });
    connect(choose_, &QPushButton::clicked, this, [this] {
        const QString dir = hooks_.chooseFolder ? hooks_.chooseFolder() : QString();
        if (!dir.isEmpty()) scanPath(dir);
    });
    connect(map_, &TreemapWidget::nodeSelected, this, [this](const linux::ScanNode* n) {
        selectPath(pathOf(n));
        // a file of the largest list: select its row too
        for (int r = 0; r < largest_->rowCount(); ++r) {
            if (largest_->item(r, 0)->data(Qt::UserRole).toString() == selectedPath_) {
                largest_->selectRow(r);
                break;
            }
        }
    });
    connect(map_, &TreemapWidget::nodeOpened, this, &DiskSpacePage::openFolder);
    connect(largest_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto rows = largest_->selectionModel()->selectedRows();
        if (!rows.isEmpty()) selectPath(largest_->item(rows.first().row(), 0)->data(Qt::UserRole).toString());
    });
    connect(filter_, &QLineEdit::textChanged, this, &DiskSpacePage::fillLargest);
    connect(largest_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](int c, Qt::SortOrder o) {
        sortColumn_ = c;
        descending_ = o == Qt::DescendingOrder;
        fillLargest();
    });
    connect(reveal_, &QPushButton::clicked, this, &DiskSpacePage::revealSelected);
    connect(trash_, &QPushButton::clicked, this, &DiskSpacePage::trashSelected);
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(selectedPath_); });
    updateButtons();
}

DiskSpacePage::~DiskSpacePage() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

void DiskSpacePage::setHooks(Hooks h) {
    if (h.confirm) hooks_.confirm = std::move(h.confirm);
    if (h.chooseFolder) hooks_.chooseFolder = std::move(h.chooseFolder);
    if (h.reveal) hooks_.reveal = std::move(h.reveal);
    if (h.run) hooks_.run = std::move(h.run);
}

void DiskSpacePage::setScanBlocked(bool blocked, const QString& reason) {
    blocked_ = blocked;
    blockReason_ = reason;
    scan_->setEnabled(!blocked || scanning_);
    scan_->setToolTip(blocked ? reason : QString());
    choose_->setEnabled(!blocked);
}

void DiskSpacePage::scanPath(const QString& path) {
    if (scanning_ || blocked_) return;
    if (worker_.joinable()) worker_.join();
    result_.reset();  // the previous result is freed before the new scan starts
    trail_.clear();
    selectedPath_.clear();
    map_->setNode(nullptr);
    largest_->setRowCount(0);
    types_->setRowCount(0);
    summary_->clear();
    scanning_ = true;
    cancel_ = false;
    scan_->setText("Cancel");
    progress_->setText("Starting…");
    emit scanRunningChanged(true);
    linux::ScanOptions opt;
    opt.root = path.toStdString();
    rootPath_ = opt.root.string();
    QPointer<DiskSpacePage> guard(this);
    worker_ = std::thread([this, guard, opt, fn = scanFn_] {
        auto result = std::make_shared<linux::ScanResult>(fn(opt, cancel_, [guard](const linux::ScanProgress& p) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, p] {
                if (!guard) return;
                guard->progress_->setText(QString("%1 files · %2 folders · %3 — %4")
                                              .arg(p.files)
                                              .arg(p.dirs)
                                              .arg(q(pmp::format(Metric::measured(static_cast<double>(p.bytes), Unit::Bytes))), q(p.current)));
            }, Qt::QueuedConnection);
        }));
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, result]() mutable {
            if (guard) guard->finishScan(std::move(result));
        }, Qt::QueuedConnection);
    });
}

void DiskSpacePage::cancelScan() {
    if (scanning_) cancel_ = true;
}

void DiskSpacePage::finishScan(std::shared_ptr<linux::ScanResult> r) {
    if (worker_.joinable()) worker_.join();
    scanning_ = false;
    scan_->setText("Scan");
    scan_->setEnabled(!blocked_);
    progress_->clear();
    result_ = std::move(r);
    showResult();
    emit scanRunningChanged(false);
    emit scanFinished();
}

void DiskSpacePage::showResult() {
    trail_.clear();
    if (!result_) return;
    trail_.push_back(&result_->root);
    map_->setNode(&result_->root);
    summary_->setText(q(vm::scanSummary(*result_)));
    summary_->setToolTip(q(vm::explainSkipped(*result_)));
    crumbs_->setText(q(result_->root.name));
    fillLargest();
    fillTypes();
    updateButtons();
}

void DiskSpacePage::fillLargest() {
    largest_->setRowCount(0);
    if (!result_) return;
    static const char* keys[] = {"name", "size", "folder", "modified", "type"};
    const auto rows = vm::buildLargest(*result_, filter_->text().toStdString(), keys[std::clamp(sortColumn_, 0, 4)], descending_);
    largest_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        auto* name = new QTableWidgetItem(q(r.name));
        name->setData(Qt::UserRole, q(r.key));
        auto* size = new QTableWidgetItem(q(r.size));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        largest_->setItem(i, 0, name);
        largest_->setItem(i, 1, size);
        largest_->setItem(i, 2, new QTableWidgetItem(q(r.folder)));
        largest_->setItem(i, 3, new QTableWidgetItem(q(r.modified)));
        largest_->setItem(i, 4, new QTableWidgetItem(q(r.type)));
    }
}

void DiskSpacePage::fillTypes() {
    types_->setRowCount(0);
    if (!result_) return;
    const auto rows = vm::buildTypes(*result_);
    types_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        types_->setItem(i, 0, new QTableWidgetItem(q(r.type)));
        types_->setItem(i, 1, new QTableWidgetItem(q(r.files)));
        types_->setItem(i, 2, new QTableWidgetItem(q(r.size)));
        types_->setItem(i, 3, new QTableWidgetItem(QString("%1%").arg(r.fraction * 100.0, 0, 'f', 1)));
    }
}

QString DiskSpacePage::pathOf(const linux::ScanNode* n) const {
    if (!n || n->isOther) return {};
    QString base = q(rootPath_);
    for (std::size_t i = 1; i < trail_.size(); ++i) base += (base.endsWith('/') ? "" : "/") + q(trail_[i]->rawName);
    if (n == trail_.back()) return base;
    return base + (base.endsWith('/') ? "" : "/") + q(n->rawName);
}

void DiskSpacePage::openFolder(const linux::ScanNode* node) {
    if (!node || !node->isDir || trail_.empty()) return;
    trail_.push_back(node);
    map_->setNode(node);
    QString crumbs = q(trail_.front()->name);
    for (std::size_t i = 1; i < trail_.size(); ++i) crumbs += " › " + q(trail_[i]->name);
    crumbs_->setText(crumbs);
    selectedPath_.clear();
    updateButtons();
}

void DiskSpacePage::up() {
    if (trail_.size() <= 1) return;
    trail_.pop_back();
    map_->setNode(trail_.back());
    QString crumbs = q(trail_.front()->name);
    for (std::size_t i = 1; i < trail_.size(); ++i) crumbs += " › " + q(trail_[i]->name);
    crumbs_->setText(crumbs);
    selectedPath_.clear();
    updateButtons();
}

void DiskSpacePage::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Backspace) {
        up();
        return;
    }
    QWidget::keyPressEvent(e);
}

void DiskSpacePage::selectPath(const QString& path) {
    selectedPath_ = path;
    updateButtons();
}

void DiskSpacePage::updateButtons() {
    const bool has = !selectedPath_.isEmpty();
    reveal_->setEnabled(has);
    trash_->setEnabled(has);
    copy_->setEnabled(has);
}

void DiskSpacePage::revealSelected() {
    if (selectedPath_.isEmpty()) return;
    const QFileInfo fi(selectedPath_);
    hooks_.reveal(fi.isDir() ? selectedPath_ : fi.absolutePath());
}

void DiskSpacePage::trashSelected() {
    if (selectedPath_.isEmpty()) return;
    const QString path = selectedPath_;
    if (!hooks_.confirm("Move to Trash?", "Move this to the Trash? You can restore it from there.\n\n" + path)) return;
    // The path goes after "--" as one argument, so a name starting with a dash or holding a newline is just a name.
    const auto r = hooks_.run(std::vector<std::string>{"gio", "trash", "--", path.toStdString()});
    if (r.exitCode != 0) {
        status_->setText("Could not move it to the Trash: " + q(linux::describeFailure(r)));
        return;
    }
    status_->setText(removeFromResult(path) ? "Moved to the Trash." : "Moved to the Trash. Scan again to refresh the map.");
}

// Removes a trashed file or folder from the kept results, subtracting its size from every folder above it.
bool DiskSpacePage::removeFromResult(const QString& path) {
    if (!result_) return false;
    const std::string target = path.toStdString();
    // the largest-files list and the type table
    std::uint64_t removedBytes = 0, removedFiles = 0;
    auto& L = result_->largest;
    for (auto it = L.begin(); it != L.end();) {
        if (it->path == target || it->path.starts_with(target + "/")) {
            auto& t = result_->byType[it->ext];
            t.first = t.first > 0 ? t.first - 1 : 0;
            t.second = t.second >= it->bytes ? t.second - it->bytes : 0;
            removedBytes += it->bytes;
            ++removedFiles;
            it = L.erase(it);
        } else {
            ++it;
        }
    }
    // the folder tree: follow the path from the scan root
    std::vector<linux::ScanNode*> chain{&result_->root};
    std::string cur = rootPath_;
    linux::ScanNode* found = nullptr;
    std::size_t index = 0;
    while (true) {
        if (target == cur) break;
        const std::string prefix = cur + (cur.ends_with('/') ? "" : "/");
        if (!target.starts_with(prefix)) {
            chain.clear();
            break;
        }
        const auto rest = target.substr(prefix.size());
        const auto slash = rest.find('/');
        const auto name = rest.substr(0, slash);
        linux::ScanNode* next = nullptr;
        for (auto& c : chain.back()->children) {
            if (c.rawName == name && !c.isOther) next = &c;
        }
        if (!next) {
            // Not kept in the map: the scan folded it into this folder's "other items" node. A file from the largest
            // list has a known size, so that node (and every folder above it) can still be reduced exactly.
            auto& kids = chain.back()->children;
            auto other = std::find_if(kids.begin(), kids.end(), [](const linux::ScanNode& n) { return n.isOther; });
            if (removedBytes > 0 && other != kids.end()) {
                auto take = [&](linux::ScanNode& n) {
                    n.bytes = n.bytes >= removedBytes ? n.bytes - removedBytes : 0;
                    n.files = n.files >= removedFiles ? n.files - removedFiles : 0;
                };
                take(*other);
                for (auto* c : chain) take(*c);
                summary_->setText(q(vm::scanSummary(*result_)));
                selectedPath_.clear();
                fillLargest();
                fillTypes();
                updateButtons();
                return true;
            }
            chain.clear();
            break;
        }
        chain.push_back(next);
        cur = prefix + name;
    }
    if (chain.size() < 2) {
        fillLargest();
        fillTypes();
        return false;
    }
    found = chain.back();
    const auto bytes = found->bytes;
    const auto files = found->isDir ? found->files : 1;
    auto& siblings = chain[chain.size() - 2]->children;
    siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [found](const linux::ScanNode& n) { return &n == found; }), siblings.end());
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        chain[i]->bytes = chain[i]->bytes >= bytes ? chain[i]->bytes - bytes : 0;
        chain[i]->files = chain[i]->files >= files ? chain[i]->files - files : 0;
    }
    (void)removedBytes;
    (void)index;
    // the trail may point into the erased node: restart it at the scan root
    trail_.assign(1, &result_->root);
    map_->setNode(&result_->root);
    crumbs_->setText(q(result_->root.name));
    summary_->setText(q(vm::scanSummary(*result_)));
    selectedPath_.clear();
    fillLargest();
    fillTypes();
    updateButtons();
    return true;
}

}  // namespace pmp::gui
