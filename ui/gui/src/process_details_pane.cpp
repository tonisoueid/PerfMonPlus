// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/process_details_pane.hpp>

#include <perfmonplus/gui/async.hpp>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace pmp::gui {
namespace {

QLabel* plain(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);  // text from other programs is never interpreted as markup
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

constexpr const char* kSectionNames[5] = {"IDENTITY", "LIFETIME", "PROCESSOR", "MEMORY", "DISK"};

}  // namespace

ProcessDetailsPane::ProcessDetailsPane(std::shared_ptr<const linux::ProcessInspector> inspector, QWidget* parent)
    : QFrame(parent), inspector_(std::move(inspector)) {
    setObjectName("card");
    title_ = plain({}, this);
    QFont big = title_->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.2);
    title_->setFont(big);
    path_ = plain({}, this);
    status_ = plain({}, this);
    exitNote_ = plain({}, this);
    exitNote_->setWordWrap(true);
    openFiles_ = plain("—", this);

    auto* grid = new QGridLayout;
    for (int s = 0; s < 5; ++s) {
        auto& col = columns_[static_cast<std::size_t>(s)];
        col.header = plain(kSectionNames[s], this);
        col.header->setEnabled(false);  // dim, as a section caption
        grid->addWidget(col.header, 0, s * 2, 1, 2);
        for (int r = 0; r < 4; ++r) {
            col.caption[static_cast<std::size_t>(r)] = plain({}, this);
            col.value[static_cast<std::size_t>(r)] = plain({}, this);
            col.caption[static_cast<std::size_t>(r)]->setEnabled(false);
            grid->addWidget(col.caption[static_cast<std::size_t>(r)], r + 1, s * 2);
            grid->addWidget(col.value[static_cast<std::size_t>(r)], r + 1, s * 2 + 1);
        }
        grid->setColumnStretch(s * 2 + 1, 1);
    }

    showCmdline_ = new QPushButton("Show command line", this);
    cmdline_ = new QPlainTextEdit(this);
    cmdline_->setReadOnly(true);
    cmdline_->setMaximumHeight(60);
    cmdline_->hide();
    env_ = new QPlainTextEdit(this);
    env_->setReadOnly(true);
    env_->setMaximumHeight(90);
    env_->hide();

    cpuGraph_ = new HistoryGraph(this);
    memGraph_ = new HistoryGraph(this);
    for (auto* g : {cpuGraph_, memGraph_}) {
        g->setMinimumHeight(60);
        g->setMaximumHeight(80);
    }
    auto* cursor = new GraphCursorHub(this);
    cpuGraph_->setCursorHub(cursor);
    memGraph_->setCursorHub(cursor);
    cpuGraph_->setCaption("CPU");
    memGraph_->setCaption("Memory");

    auto* graphs = new QVBoxLayout;
    graphs->addWidget(cpuGraph_);
    graphs->addWidget(memGraph_);
    auto* facts = new QVBoxLayout;
    facts->addLayout(grid);
    auto* extras = new QHBoxLayout;
    extras->addWidget(openFiles_);
    extras->addStretch(1);
    extras->addWidget(showCmdline_);
    facts->addLayout(extras);
    facts->addWidget(cmdline_);
    facts->addWidget(env_);
    auto* body = new QHBoxLayout;
    body->addLayout(facts, 3);
    body->addLayout(graphs, 1);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 10, 14, 10);
    auto* head = new QHBoxLayout;
    head->addWidget(title_);
    head->addStretch(1);
    head->addWidget(status_);
    col->addLayout(head);
    col->addWidget(path_);
    col->addWidget(exitNote_);
    col->addLayout(body);

    connect(showCmdline_, &QPushButton::clicked, this, [this] {
        commandVisible_ = !commandVisible_;
        showInspection();
    });
    timer_.setInterval(5000);
    connect(&timer_, &QTimer::timeout, this, [this] { inspect(); });
}

void ProcessDetailsPane::setTheme(const vm::Theme& theme, bool bloom) {
    for (auto* g : {cpuGraph_, memGraph_}) {
        g->setTheme(theme);
        g->setBloom(bloom);
    }
}

void ProcessDetailsPane::setProcess(const ProcessRecord* record, const ProcessRecord* parent) {
    const bool sameProcess = record && record_ && record->key == record_->key;
    record_ = record ? std::optional<ProcessRecord>(*record) : std::nullopt;
    parent_ = parent ? std::optional<ProcessRecord>(*parent) : std::nullopt;
    if (!sameProcess) {
        inspection_.reset();
        exited_ = false;
        commandVisible_ = false;  // the command line is hidden again for every new selection
        ++request_;
    }
    if (!record_) {
        timer_.stop();
        cpuRing_.reset();
        memRing_.reset();
        cpuGraph_->clearSeries();
        memGraph_->clearSeries();
        render();
        return;
    }
    render();
    if (!sameProcess) {
        inspect();
        if (isVisible()) timer_.start();  // a hidden pane starts ticking when it is shown
    }
}

// The 5 s inspection exists to keep open-file counts fresh for what is on screen: nothing to refresh while hidden.
void ProcessDetailsPane::hideEvent(QHideEvent* e) {
    QFrame::hideEvent(e);
    timer_.stop();
}

void ProcessDetailsPane::showEvent(QShowEvent* e) {
    QFrame::showEvent(e);
    if (record_ && !timer_.isActive()) {
        inspect();  // what was on screen when it was hidden is stale
        timer_.start();
    }
}

void ProcessDetailsPane::setShowEnvironment(bool on) {
    if (showEnvironment_ == on) return;
    showEnvironment_ = on;
    if (record_) inspect();  // pick the environment up (or drop it) now
    showInspection();
}

void ProcessDetailsPane::inspect() {
    if (!record_ || !inspector_) return;
    ++inspections_;
    const std::uint64_t request = ++request_;
    const auto inspector = inspector_;
    const ProcessKey key = record_->key;
    const bool withEnvironment = showEnvironment_;
    runAsync<std::optional<linux::Inspection>>(
        this, [inspector, key, withEnvironment] { return inspector->inspect(key, withEnvironment); },
        [this, request](std::optional<linux::Inspection> result) {
            if (request != request_) return;  // the selection moved on
            exited_ = !result.has_value();
            if (result) inspection_ = std::move(result);
            render();
            showInspection();
            emit inspected();
        });
}

void ProcessDetailsPane::update(const Frame& frame, const ProcessHistory* history) {
    if (!record_) return;
    ctx_.now = frame.mono;
    ctx_.wallNow = frame.wall;
    ctx_.totalMemoryBytes = frame.memory.total.isMeasured() ? *frame.memory.total.value : 0;
    // The record behind the selection may have new numbers, or may have just become a tombstone.
    for (const auto& r : frame.processes.processes) {
        if (r.key == record_->key) {
            record_ = r;
            break;
        }
    }
    if (record_->parent) {
        parent_.reset();
        for (const auto& r : frame.processes.processes) {
            if (r.key == *record_->parent) {
                parent_ = r;
                break;
            }
        }
    }
    if (history) {
        cpuRing_ = history->cpuSeries(record_->key);
        memRing_ = history->memorySeries(record_->key);
    } else {
        cpuRing_.reset();
        memRing_.reset();
    }
    cpuGraph_->clearSeries();
    memGraph_->clearSeries();
    if (cpuRing_) cpuGraph_->addSeries(&*cpuRing_, vm::darkTheme().cpu, "CPU", Unit::Percent);
    if (memRing_) memGraph_->addSeries(&*memRing_, vm::darkTheme().memory, "Memory", Unit::Bytes);
    cpuGraph_->setNow(frame.mono);
    memGraph_->setNow(frame.mono);
    cpuGraph_->setAutoScale(5.0);
    memGraph_->setAutoScale(1024.0 * 1024.0);
    render();
}

void ProcessDetailsPane::render() {
    if (!record_) {
        title_->clear();
        path_->clear();
        status_->clear();
        exitNote_->clear();
        openFiles_->setText("—");
        for (auto& c : columns_) {
            c.header->clear();
            for (std::size_t i = 0; i < 4; ++i) {
                c.caption[i]->clear();
                c.value[i]->clear();
            }
        }
        return;
    }
    const auto d = vm::processDetails(*record_, parent_ ? &*parent_ : nullptr, inspection_ ? &*inspection_ : nullptr, ctx_);
    title_->setText(QString::fromStdString(d.title));
    path_->setText(QString::fromStdString(d.path));
    exitNote_->setText(QString::fromStdString(d.exitText));
    exitNote_->setVisible(!d.exitText.empty());
    status_->setText(exited_ && !record_->exitedAt ? QString("This process has exited") : QString());
    openFiles_->setText(QString::fromStdString(d.openFiles));
    for (std::size_t s = 0; s < d.sections.size() && s < 5; ++s) {
        auto& col = columns_[s];
        col.header->setText(QString::fromStdString(d.sections[s].first));
        for (std::size_t r = 0; r < 4; ++r) {
            const bool have = r < d.sections[s].second.size();
            col.caption[r]->setText(have ? QString::fromStdString(d.sections[s].second[r].caption) : QString());
            col.value[r]->setText(have ? QString::fromStdString(d.sections[s].second[r].value) : QString());
        }
    }
}

void ProcessDetailsPane::showInspection() {
    showCmdline_->setText(commandVisible_ ? "Hide command line" : "Show command line");
    cmdline_->setVisible(commandVisible_);
    if (commandVisible_) {
        QString text = "…";
        if (inspection_) {
            text = inspection_->cmdlineRestricted ? QString("Restricted")
                   : inspection_->cmdline.empty()  ? QString("—")
                                                   : QString::fromStdString(inspection_->cmdline);
        }
        cmdline_->setPlainText(text);
    }
    env_->setVisible(showEnvironment_ && record_.has_value());
    if (showEnvironment_ && inspection_) {
        QString text = inspection_->environmentRestricted ? QString("Restricted")
                       : inspection_->environment.empty() ? QString("—")
                                                          : QString::fromStdString(inspection_->environment);
        if (inspection_->environmentTruncated) text += "\n…";
        env_->setPlainText(text);
    } else if (showEnvironment_) {
        env_->setPlainText("…");
    }
}

QString ProcessDetailsPane::valueOf(const QString& section, const QString& caption) const {
    for (const auto& c : columns_) {
        if (c.header->text() != section) continue;
        for (std::size_t i = 0; i < 4; ++i) {
            if (c.caption[i]->text() == caption) return c.value[i]->text();
        }
    }
    return {};
}

}  // namespace pmp::gui
