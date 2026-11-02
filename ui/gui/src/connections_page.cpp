// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/connections_page.hpp>

#include <perfmonplus/gui/async.hpp>

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QVBoxLayout>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <map>

namespace pmp::gui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

linux::ConnectionList collectLive() {
    static linux::ConnectionsCollector collector(linux::ProcRoot::live());
    return collector.collect();
}

std::string resolveLive(const linux::IpAddr& a) {
    char host[NI_MAXHOST] = {};
    int rc = -1;
    if (a.v6) {
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        std::copy(a.bytes.begin(), a.bytes.end(), sa.sin6_addr.s6_addr);
        rc = getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof sa, host, sizeof host, nullptr, 0, NI_NAMEREQD);
    } else {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        std::copy(a.bytes.begin(), a.bytes.begin() + 4, reinterpret_cast<std::uint8_t*>(&sa.sin_addr));
        rc = getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof sa, host, sizeof host, nullptr, 0, NI_NAMEREQD);
    }
    return rc == 0 ? std::string(host) : std::string();
}

}  // namespace

void ConnectionsModel::setRows(std::vector<vm::ConnectionRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

QVariant ConnectionsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const auto& r = rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return q(r.process);
            case 1: return q(r.protocol);
            case 2: return q(r.local);
            case 3: return q(r.remote);
            case 4: return q(r.state);
            case 5: return q(r.rx);
            case 6: return q(r.tx);
            case 7: return q(r.host);
        }
    }
    if (role == Qt::ToolTipRole) {
        if (!r.exposure.empty() && index.column() <= 4) return q(r.exposure);
        if (index.column() == 5 || index.column() == 6) {
            return QStringLiteral("Per-connection traffic needs a privileged helper, which PerfMonPlus does not run.");
        }
    }
    if (role == Qt::ForegroundRole && r.exposed) return QColor(0xf5, 0xa5, 0x24);
    if (role == Qt::TextAlignmentRole && (index.column() == 5 || index.column() == 6)) return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant ConnectionsModel::headerData(int section, Qt::Orientation o, int role) const {
    static const char* names[] = {"Process", "Protocol", "Local address", "Remote address", "State", "Rx/s", "Tx/s", "Remote host"};
    if (o == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < 8) return QString::fromUtf8(names[section]);
    return {};
}

const char* ConnectionsModel::sortKey(int column) {
    static const char* keys[] = {"process", "protocol", "local", "remote", "state", "state", "state", "host"};
    return column >= 0 && column < 8 ? keys[column] : "process";
}

ConnectionsPage::ConnectionsPage(CollectFn collect, QWidget* parent)
    : QWidget(parent), collect_(collect ? std::move(collect) : CollectFn(collectLive)), resolve_(resolveLive) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    col->setSpacing(6);

    auto* bar = new QHBoxLayout;
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Filter by process, port, address or state");
    filter_->setClearButtonEnabled(true);
    listening_ = new QCheckBox("Listening / Exposed", this);
    proto_ = new QComboBox(this);
    proto_->addItem("All protocols", -1);
    for (auto [label, p] : {std::pair{"TCP", linux::Proto::Tcp4}, {"TCP6", linux::Proto::Tcp6}, {"UDP", linux::Proto::Udp4}, {"UDP6", linux::Proto::Udp6}}) {
        proto_->addItem(label, static_cast<int>(p));
    }
    state_ = new QComboBox(this);
    state_->addItem("Any state", -1);
    for (auto [label, s] : {std::pair{"LISTEN", linux::SockState::Listen}, {"ESTABLISHED", linux::SockState::Established},
                            {"TIME_WAIT", linux::SockState::TimeWait}, {"CLOSE_WAIT", linux::SockState::CloseWait},
                            {"SYN_SENT", linux::SockState::SynSent}}) {
        state_->addItem(label, static_cast<int>(s));
    }
    go_ = new QPushButton("Go to process", this);
    copy_ = new QPushButton("Copy", this);
    bar->addWidget(filter_, 1);
    bar->addWidget(proto_);
    bar->addWidget(state_);
    bar->addWidget(listening_);
    bar->addWidget(go_);
    bar->addWidget(copy_);
    col->addLayout(bar);

    note_ = new QLabel(QString::fromStdString(vm::exposureExplanation()), this);
    note_->setObjectName("dim");
    note_->setWordWrap(true);
    note_->setTextFormat(Qt::PlainText);
    note_->hide();
    col->addWidget(note_);

    model_ = new ConnectionsModel(this);
    table_ = new QTableView(this);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    table_->setAccessibleName("Connections");
    table_->setColumnWidth(0, 200);
    table_->setColumnWidth(2, 190);
    table_->setColumnWidth(3, 190);
    col->addWidget(table_, 1);
    status_ = new QLabel(this);
    status_->setObjectName("dim");
    status_->setTextFormat(Qt::PlainText);
    col->addWidget(status_);

    timer_.setInterval(2000);  // polled only while the page is showing (NFR-04)
    connect(&timer_, &QTimer::timeout, this, &ConnectionsPage::refresh);
    connect(filter_, &QLineEdit::textChanged, this, &ConnectionsPage::rebuild);
    connect(listening_, &QCheckBox::toggled, this, [this](bool on) {
        note_->setVisible(on);
        rebuild();
    });
    connect(proto_, &QComboBox::currentIndexChanged, this, &ConnectionsPage::rebuild);
    connect(state_, &QComboBox::currentIndexChanged, this, &ConnectionsPage::rebuild);
    connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        descending_ = order == Qt::DescendingOrder;
        rebuild();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateButtons(); });
    connect(go_, &QPushButton::clicked, this, &ConnectionsPage::goToProcess);
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(copyText()); });
    updateButtons();
}

vm::ConnectionOptions ConnectionsPage::options() const {
    vm::ConnectionOptions o;
    o.listeningOnly = listening_->isChecked();
    o.filter = filter_->text().toStdString();
    if (proto_->currentData().toInt() >= 0) o.proto = static_cast<linux::Proto>(proto_->currentData().toInt());
    if (state_->currentData().toInt() >= 0) o.state = static_cast<linux::SockState>(state_->currentData().toInt());
    o.sortBy = ConnectionsModel::sortKey(sortColumn_);
    o.descending = descending_;
    o.resolveHosts = resolveHosts_;
    return o;
}

std::size_t ConnectionsPage::namesSignature() const {
    // Only the names of processes that own a socket matter. A hash keeps this cheap enough to run every sample.
    std::map<ProcessKey, const ProcessRecord*> byKey;
    for (const auto& p : processes_.processes.processes) {
        if (!p.exitedAt) byKey[p.key] = &p;
    }
    std::size_t h = 1469598103934665603ULL;
    for (const auto& c : list_.rows) {
        if (!c.owner) continue;
        const auto it = byKey.find(*c.owner);
        h = (h ^ (it == byKey.end() ? 0 : std::hash<std::string>{}(it->second->name) + 1)) * 1099511628211ULL;
    }
    return h;
}

void ConnectionsPage::update(const Frame& frame, const HistoryStore&, MonoTime) {
    processes_.processes = frame.processes;
    if (list_.rows.empty()) return;
    const auto sig = namesSignature();
    if (sig != namesSig_) rebuild();  // a name appeared or changed; otherwise there is nothing new to show
}

void ConnectionsPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!paused_) {
        refresh();
        timer_.start();
    }
}

void ConnectionsPage::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    timer_.stop();
}

void ConnectionsPage::setPaused(bool paused) {
    paused_ = paused;
    if (paused) timer_.stop();
    else if (isVisible()) {
        refresh();
        timer_.start();
    }
}

void ConnectionsPage::refresh() {
    if (inFlight_ || paused_) return;
    inFlight_ = true;
    runAsync<linux::ConnectionList>(this, collect_, [this](linux::ConnectionList list) {
        inFlight_ = false;
        apply(std::move(list));
    });
}

void ConnectionsPage::refreshSync() { apply(collect_()); }

void ConnectionsPage::apply(linux::ConnectionList list) {
    const bool same = list.rows.size() == list_.rows.size() && list.truncated == list_.truncated &&
                      list.anyUnreadableOwners == list_.anyUnreadableOwners &&
                      std::equal(list.rows.begin(), list.rows.end(), list_.rows.begin(), [](const linux::Connection& a, const linux::Connection& b) {
                          return a.proto == b.proto && a.local == b.local && a.remote == b.remote && a.localPort == b.localPort &&
                                 a.remotePort == b.remotePort && a.state == b.state && a.inode == b.inode && a.owner == b.owner &&
                                 a.ownerAvailability == b.ownerAvailability;
                      });
    list_ = std::move(list);
    if (same && model_->rowCount() > 0) {  // an unchanged table would only reset the view and lose the scroll position...
        if (resolveHosts_ && expireHosts()) rebuild();  // ...unless a looked-up name has gone stale and must be asked for again
        return;
    }
    rebuild();
}

void ConnectionsPage::setResolveHosts(bool on) {
    if (resolveHosts_ == on) return;
    resolveHosts_ = on;
    if (!on) {
        hosts_ = {};  // turning it off forgets what was looked up
        lookedUpAt_.clear();
    }
    rebuild();
}

bool ConnectionsPage::expireHosts() {
    bool any = false;
    const auto now = now_();
    for (auto it = lookedUpAt_.begin(); it != lookedUpAt_.end();) {
        const bool ok = hosts_.resolved.contains(it->first);
        if (now - it->second >= (ok ? kResolvedFor : kFailedFor)) {
            hosts_.resolved.erase(it->first);
            hosts_.failed.erase(it->first);
            it = lookedUpAt_.erase(it);  // asked again the next time it is shown
            any = true;
        } else {
            ++it;
        }
    }
    return any;
}

void ConnectionsPage::rebuild() {
    expireHosts();
    const QString keep = selectedKey();
    const int scroll = table_->verticalScrollBar()->value();
    auto rows = vm::buildConnections(list_.rows, processes_, options(), hosts_);
    model_->setRows(std::move(rows));
    namesSig_ = namesSignature();
    if (!keep.isEmpty()) selectKey(keep);
    table_->verticalScrollBar()->setValue(scroll);
    int exposed = 0;
    for (int i = 0; i < model_->rowCount(); ++i) exposed += model_->rowAt(i).exposed ? 1 : 0;
    QString text = QString("%1 connections").arg(model_->rowCount());
    if (exposed > 0) text += QString(" · %1 exposed").arg(exposed);
    if (list_.anyUnreadableOwners) text += " · some processes belong to other users (Restricted)";
    if (list_.truncated > 0) text += QString(" · +%1 more not shown").arg(list_.truncated);
    status_->setText(text);
    updateButtons();
    if (resolveHosts_) startLookups();
}

void ConnectionsPage::startLookups() {
    int started = 0;
    for (int i = 0; i < model_->rowCount() && started < 20; ++i) {  // visible rows only, a few at a time
        const auto& row = model_->rowAt(i);
        if (!row.remoteAddr || row.host != "Resolving…") continue;
        const auto addr = *row.remoteAddr;
        if (!hosts_.pending.insert(addr).second) continue;
        ++started;
        const auto fn = resolve_;
        runAsync<std::pair<linux::IpAddr, std::string>>(
            this, [addr, fn] { return std::make_pair(addr, fn ? fn(addr) : std::string()); },
            [this](std::pair<linux::IpAddr, std::string> r) {
                hosts_.pending.erase(r.first);
                lookedUpAt_[r.first] = now_();
                if (r.second.empty()) hosts_.failed.insert(r.first);
                else hosts_.resolved[r.first] = r.second;
                if (resolveHosts_) rebuild();
            });
    }
}

QString ConnectionsPage::selectedKey() const {
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? QString() : q(model_->rowAt(rows.first().row()).key);
}

void ConnectionsPage::selectKey(const QString& key) {
    for (int i = 0; i < model_->rowCount(); ++i) {
        if (q(model_->rowAt(i).key) == key) {
            table_->selectRow(i);
            return;
        }
    }
}

bool ConnectionsPage::canGoToProcess() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return false;
    const auto& r = model_->rowAt(rows.first().row());
    if (!r.owner) return false;
    for (const auto& p : processes_.processes.processes) {
        if (p.key == *r.owner && !p.exitedAt) return true;  // the identity is re-checked against the latest frame
    }
    return false;
}

void ConnectionsPage::goToProcess() {
    if (!canGoToProcess()) return;
    emit processRequested(*model_->rowAt(table_->selectionModel()->selectedRows().first().row()).owner);
}

QString ConnectionsPage::copyText() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return {};
    const auto& r = model_->rowAt(rows.first().row());
    QStringList cells;
    for (const auto* s : {&r.process, &r.protocol, &r.local, &r.remote, &r.state}) cells << q(*s);
    return cells.join('\t');
}

void ConnectionsPage::updateButtons() {
    go_->setEnabled(canGoToProcess());
    copy_->setEnabled(!table_->selectionModel()->selectedRows().isEmpty());
}

}  // namespace pmp::gui
