// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/system_info_page.hpp>

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace pmp::gui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

const char* healthText(HealthState s) {
    switch (s) {
        case HealthState::Healthy: return "Healthy";
        case HealthState::Degraded: return "Degraded";
        case HealthState::Failed: return "Failed";
        case HealthState::Disabled: return "Disabled";
    }
    return "Unknown";
}

}  // namespace

SystemInfoPage::SystemInfoPage(QWidget* parent)
    : QWidget(parent),
      list_(new QListWidget(this)),
      table_(new QTableWidget(0, 2, this)),
      includeIdentifiers_(new QCheckBox("Include identifiers (serial numbers, MAC addresses)", this)) {
    list_->setFixedWidth(200);
    list_->setFrameShape(QFrame::NoFrame);
    table_->setHorizontalHeaderLabels({"Property", "Value"});
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);

    auto* copy = new QPushButton("Copy", this);
    auto* saveText = new QPushButton("Export text…", this);
    auto* saveJson = new QPushButton("Export JSON…", this);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(includeIdentifiers_);
    buttons->addStretch(1);
    buttons->addWidget(copy);
    buttons->addWidget(saveText);
    buttons->addWidget(saveJson);

    auto* right = new QVBoxLayout;
    right->addWidget(table_, 1);
    right->addLayout(buttons);
    exportStatus_ = new QLabel(this);
    exportStatus_->setObjectName("dim");
    exportStatus_->setTextFormat(Qt::PlainText);  // the message holds a path
    right->addWidget(exportStatus_);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 4, 14, 10);
    row->addWidget(list_);
    row->addLayout(right, 1);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { rebuild(); });
    connect(includeIdentifiers_, &QCheckBox::toggled, this, [this] { rebuild(); });
    connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(exportText()); });
    const auto save = [this](const QString& filter, bool json) {
        const QString path = QFileDialog::getSaveFileName(this, "Export system information", QString(), filter);
        if (!path.isEmpty()) saveTo(path, json);
    };
    connect(saveText, &QPushButton::clicked, this, [save] { save("Text (*.txt)", false); });
    connect(saveJson, &QPushButton::clicked, this, [save] { save("JSON (*.json)", true); });
    rebuild();
}

void SystemInfoPage::setInfo(const linux::SystemInfo& info) {
    info_ = info;
    rebuild();
}

void SystemInfoPage::update(const Frame& frame, const HistoryStore&, MonoTime) {
    // The Sensors section lists every provider's state and last error: any change in any of them is news.
    bool changed = frame.providerHealth.size() != health_.size();
    for (std::size_t i = 0; !changed && i < health_.size(); ++i) {
        changed = frame.providerHealth[i].state != health_[i].state || frame.providerHealth[i].lastError != health_[i].lastError;
    }
    health_ = frame.providerHealth;
    if (changed) rebuild();
}

std::vector<vm::InfoSection> SystemInfoPage::sections() const {
    auto out = vm::systemInfoSections(info_, includeIdentifiers_->isChecked());

    vm::InfoSection displays{"Displays", {}};
    for (const QScreen* s : QGuiApplication::screens()) {
        displays.rows.push_back({s->name().toStdString(),
                                 QString("%1×%2 @ %3 Hz (scale %4)")
                                     .arg(s->size().width())
                                     .arg(s->size().height())
                                     .arg(s->refreshRate(), 0, 'f', 0)
                                     .arg(s->devicePixelRatio())
                                     .toStdString(),
                                 false});
    }
    if (displays.rows.empty()) displays.rows.push_back({"Display", "Unavailable", false});
    out.push_back(std::move(displays));

    vm::InfoSection sensors{"Sensors", {}};  // which data sources are working, and why not (SYS-01)
    for (const auto& h : health_) {
        sensors.rows.push_back({h.id, std::string(healthText(h.state)) + (h.lastError.empty() ? "" : ": " + h.lastError), false});
    }
    if (sensors.rows.empty()) sensors.rows.push_back({"Providers", "Waiting for the first sample", false});
    out.push_back(std::move(sensors));
    return out;
}

void SystemInfoPage::rebuild() {
    const auto secs = sections();
    const QString current = list_->currentItem() ? list_->currentItem()->text() : QString();
    {
        const QSignalBlocker block(list_);
        list_->clear();
        for (const auto& s : secs) list_->addItem(q(s.title));
        int row = 0;
        for (int i = 0; i < list_->count(); ++i) {
            if (list_->item(i)->text() == current) row = i;
        }
        list_->setCurrentRow(row);
    }
    const auto idx = static_cast<std::size_t>(std::max(0, list_->currentRow()));
    const auto& rows = secs[idx].rows;
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        table_->setItem(r, 0, new QTableWidgetItem(q(rows[static_cast<std::size_t>(r)].key)));
        table_->setItem(r, 1, new QTableWidgetItem(q(rows[static_cast<std::size_t>(r)].value)));
    }
}

QStringList SystemInfoPage::categories() const {
    QStringList out;
    for (const auto& s : sections()) out << q(s.title);
    return out;
}

void SystemInfoPage::selectCategory(const QString& name) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->text() == name) list_->setCurrentRow(i);
    }
}

bool SystemInfoPage::saveTo(const QString& path, bool json) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        exportStatus_->setText(QString("Could not save %1: %2").arg(path, f.errorString()));
        return false;
    }
    const QByteArray bytes = (json ? exportJson() : exportText()).toUtf8();
    if (f.write(bytes) != bytes.size() || !f.flush()) {
        exportStatus_->setText(QString("Could not save %1: %2").arg(path, f.errorString()));
        return false;
    }
    exportStatus_->setText(QString("Saved %1").arg(path));
    return true;
}

QString SystemInfoPage::exportText() const { return q(vm::exportText(sections())); }

QString SystemInfoPage::exportJson() const {
    QJsonArray secs;
    for (const auto& s : sections()) {
        QJsonArray rows;
        for (const auto& r : s.rows) rows.append(QJsonObject{{"key", q(r.key)}, {"value", q(r.value)}});
        secs.append(QJsonObject{{"title", q(s.title)}, {"rows", rows}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"sections", secs}}).toJson(QJsonDocument::Indented));
}

}  // namespace pmp::gui
