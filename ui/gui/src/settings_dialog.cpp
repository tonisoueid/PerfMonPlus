// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/settings_dialog.hpp>

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/core/version.hpp>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace pmp::gui {
namespace {

// Selects the entry holding `value`, adding it first when the saved value is not one of the offered choices,
// so opening and accepting the dialog never changes a setting the user did not touch.
void select(QComboBox* box, int value, const QString& fallbackLabel) {
    int i = box->findData(value);
    if (i < 0) {
        box->addItem(fallbackLabel, value);
        i = box->count() - 1;
    }
    box->setCurrentIndex(i);
}

QString stateText(HealthState s) {
    switch (s) {
        case HealthState::Healthy: return "Healthy";
        case HealthState::Degraded: return "Degraded";
        case HealthState::Failed: return "Failed";
        case HealthState::Disabled: return "Disabled";
    }
    return {};
}

QWidget* formPage(QFormLayout*& form) {
    auto* page = new QWidget;
    page->setObjectName("tabPage");
    form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setVerticalSpacing(12);
    return page;
}

}  // namespace

SettingsDialog::SettingsDialog(const AppSettings& settings, std::vector<ProviderHealth> health, QWidget* parent)
    : QDialog(parent), s_(settings), tabs_(new QTabWidget(this)) {
    setWindowTitle("PerfMonPlus settings");
    setMinimumWidth(560);
    tabs_->addTab(generalTab(), "General");
    tabs_->addTab(samplingTab(), "Sampling");
    tabs_->addTab(displayTab(), "Display");
    tabs_->addTab(privacyTab(), "Privacy");
    tabs_->addTab(advancedTab(health), "Advanced");
    tabs_->addTab(aboutTab(), "About");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* col = new QVBoxLayout(this);
    col->addWidget(tabs_);
    col->addWidget(buttons);
}

QWidget* SettingsDialog::generalTab() {
    QFormLayout* form;
    auto* page = formPage(form);
    launchAtLogin_ = new QCheckBox("Start PerfMonPlus when I log in", page);
    launchAtLogin_->setChecked(s_.launchAtLogin);
    form->addRow(launchAtLogin_);

    recRing_ = new QCheckBox("Keep a rolling recording so I can save what just happened (uses memory, nothing is written)", page);
    recRing_->setChecked(s_.recordingRing);
    recRingMinutes_ = new QSpinBox(page);
    recRingMinutes_->setRange(1, 60);
    recRingMinutes_->setSuffix(" min");
    recRingMinutes_->setValue(s_.recordingRingMinutes);
    recRingMinutes_->setEnabled(s_.recordingRing);
    recMaxMinutes_ = new QSpinBox(page);
    recMaxMinutes_->setRange(5, 120);
    recMaxMinutes_->setSuffix(" min");
    recMaxMinutes_->setValue(s_.recordingMaxMinutes);
    recTop_ = new QSpinBox(page);
    recTop_->setRange(5, 50);
    recTop_->setValue(s_.recordingTopProcesses);
    connect(recRing_, &QCheckBox::toggled, recRingMinutes_, &QWidget::setEnabled);
    videoFps_ = new QComboBox(page);
    videoFps_->addItem("15 frames per second", 15);
    videoFps_->addItem("30 frames per second", 30);
    videoFps_->setCurrentIndex(s_.videoFps == 15 ? 0 : 1);
    form->addRow("Screen video recording", videoFps_);
    form->addRow(recRing_);
    form->addRow("Rolling buffer length", recRingMinutes_);
    form->addRow("Longest recording", recMaxMinutes_);
    form->addRow("Top processes per sample", recTop_);
    return page;
}

QWidget* SettingsDialog::samplingTab() {
    QFormLayout* form;
    auto* page = formPage(form);
    interval_ = new QComboBox(page);
    for (auto [ms, label] : {std::pair{500, "0.5 s"}, {1000, "1 s"}, {2000, "2 s"}, {5000, "5 s"}, {0, "Paused"}}) {
        interval_->addItem(label, ms);
    }
    select(interval_, s_.sampleIntervalMs, QString("%1 ms").arg(s_.sampleIntervalMs));
    history_ = new QComboBox(page);
    for (auto [sec, label] : {std::pair{30, "30 seconds"}, {60, "1 minute"}, {300, "5 minutes"}, {900, "15 minutes"}, {3600, "1 hour"}}) {
        history_->addItem(label, sec);
    }
    select(history_, s_.historySeconds, QString("%1 seconds").arg(s_.historySeconds));
    topCount_ = new QSpinBox(page);
    topCount_->setRange(5, 30);
    topCount_->setValue(s_.topProcessCount);
    form->addRow("Update interval", interval_);
    form->addRow("Graph history window", history_);
    form->addRow("Top processes shown", topCount_);
    tombstone_ = new QComboBox(page);
    for (auto [sec, label] : {std::pair{0, "Off"}, {5, "5 seconds"}, {10, "10 seconds"}, {30, "30 seconds"}, {60, "60 seconds"}}) {
        tombstone_->addItem(label, sec);
    }
    select(tombstone_, s_.tombstoneSeconds, QString("%1 seconds").arg(s_.tombstoneSeconds));
    form->addRow("Keep exited processes visible for", tombstone_);
    processHistory_ = new QCheckBox("Keep a history for every process (graph hover and \"which process caused this?\")", page);
    processHistory_->setChecked(s_.processHistoryEnabled);
    form->addRow(processHistory_);
    return page;
}

QWidget* SettingsDialog::displayTab() {
    QFormLayout* form;
    auto* page = formPage(form);
    frameRate_ = new QComboBox(page);
    for (int hz : {15, 30, 60, 120}) frameRate_->addItem(QString("%1 Hz").arg(hz), hz);
    select(frameRate_, s_.frameRate, QString("%1 Hz").arg(s_.frameRate));
    bloom_ = new QCheckBox("Bloom glow on meters", page);
    bloom_->setChecked(s_.bloom);
    lowEffects_ = new QCheckBox("Low effects (30 Hz, no bloom)", page);
    lowEffects_->setChecked(s_.lowEffects);
    units_ = new QComboBox(page);
    units_->addItem("Binary (KiB = 1024 bytes)", 0);
    units_->addItem("Decimal (KB = 1000 bytes)", 1);
    units_->setCurrentIndex(s_.decimalUnits ? 1 : 0);
    temperature_ = new QComboBox(page);
    temperature_->addItem("Celsius (°C)", 0);
    temperature_->addItem("Fahrenheit (°F)", 1);
    temperature_->setCurrentIndex(s_.fahrenheit ? 1 : 0);
    form->addRow("Frame rate", frameRate_);
    form->addRow(bloom_);
    form->addRow(lowEffects_);
    form->addRow("Byte units", units_);
    form->addRow("Temperature", temperature_);
    return page;
}

QWidget* SettingsDialog::privacyTab() {
    QFormLayout* form;
    auto* page = formPage(form);
    checkUpdates_ = new QCheckBox("Check for updates when PerfMonPlus starts", page);
    checkUpdates_->setChecked(s_.checkUpdates);
    manifestUrl_ = new QLineEdit(QString::fromStdString(s_.updateManifestUrl), page);
    manifestUrl_->setPlaceholderText("https://… (empty: updates are never checked)");
    checkNow_ = new QPushButton("Check now", page);
    updateStatus_ = new QLabel(page);
    updateStatus_->setWordWrap(true);
    connect(checkNow_, &QPushButton::clicked, this, [this] { emit checkNowRequested(manifestUrl_->text().trimmed()); });
    resolveHosts_ = new QCheckBox("Look up host names for the remote addresses on the Connections page", page);
    resolveHosts_->setChecked(s_.resolveHostNames);
    resolveHosts_->setToolTip("Sends each remote address you can see to your DNS resolver. Off by default.");
    form->addRow(resolveHosts_);
    auto* note = new QLabel(
        "PerfMonPlus sends nothing anywhere unless you enter an update manifest address. The check is one plain "
        "HTTPS (or file) request with no identifiers, cookies or telemetry. Serial numbers and MAC addresses stay "
        "hidden in the interface and in exports unless you tick \"Include identifiers\" on System Info.",
        page);
    note->setWordWrap(true);
    form->addRow(checkUpdates_);
    environment_ = new QCheckBox("Show environment variables in process details (they often contain secrets)", page);
    environment_->setChecked(s_.showEnvironment);
    form->addRow(environment_);
    form->addRow("Update manifest address", manifestUrl_);
    form->addRow(checkNow_);
    form->addRow(updateStatus_);
    form->addRow(note);
    return page;
}

QWidget* SettingsDialog::advancedTab(const std::vector<ProviderHealth>& health) {
    auto* page = new QWidget;
    page->setObjectName("tabPage");
    auto* col = new QVBoxLayout(page);
    providers_ = new QTableWidget(static_cast<int>(health.size()), 4, page);
    providers_->setHorizontalHeaderLabels({"Provider (tick = enabled)", "State", "Last error", "Avg cost"});
    providers_->verticalHeader()->hide();
    providers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    providers_->setSelectionMode(QAbstractItemView::NoSelection);
    providers_->horizontalHeader()->setStretchLastSection(true);
    providers_->setColumnWidth(0, 200);
    for (int i = 0; i < static_cast<int>(health.size()); ++i) {
        const auto& h = health[static_cast<std::size_t>(i)];
        const QString id = QString::fromStdString(h.id);
        auto* name = new QTableWidgetItem(id);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        name->setCheckState(s_.disabledProviders.contains(id) ? Qt::Unchecked : Qt::Checked);
        providers_->setItem(i, 0, name);
        providers_->setItem(i, 1, new QTableWidgetItem(stateText(h.state)));
        providers_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(h.lastError)));
        providers_->setItem(i, 3, new QTableWidgetItem(QString("%1 µs").arg(h.avgSampleCostMicros, 0, 'f', 0)));
    }
    col->addWidget(providers_);
    auto* note = new QLabel("A disabled provider is never read; its numbers show as Disabled.", page);
    note->setWordWrap(true);
    col->addWidget(note);
    return page;
}

QWidget* SettingsDialog::aboutTab() {
    auto* page = new QWidget;
    page->setObjectName("tabPage");
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(16, 16, 16, 16);
    const auto version = versionString();
    col->addWidget(new QLabel("PerfMonPlus", page));
    col->addWidget(new QLabel(QString("perfmonplus %1").arg(QString::fromUtf8(version.data(), static_cast<int>(version.size()))), page));
    col->addWidget(new QLabel("Licence: GPL-3.0-or-later. Every feature is free; there is no paid edition.", page));
    auto* credits = new QLabel(
        "Built with Qt 6 and C++20. PerfMonPlus is an independent, clean-room project: it is not affiliated with "
        "or derived from any other task manager.",
        page);
    credits->setWordWrap(true);
    col->addWidget(credits);
    col->addStretch(1);
    return page;
}

AppSettings SettingsDialog::result() const {
    AppSettings r = s_;
    r.launchAtLogin = launchAtLogin_->isChecked();
    r.sampleIntervalMs = interval_->currentData().toInt();
    r.historySeconds = history_->currentData().toInt();
    r.topProcessCount = topCount_->value();
    r.tombstoneSeconds = tombstone_->currentData().toInt();
    r.processHistoryEnabled = processHistory_->isChecked();
    r.showEnvironment = environment_->isChecked();
    r.videoFps = videoFps_->currentData().toInt();
    r.resolveHostNames = resolveHosts_->isChecked();
    r.recordingRing = recRing_->isChecked();
    r.recordingRingMinutes = recRingMinutes_->value();
    r.recordingMaxMinutes = recMaxMinutes_->value();
    r.recordingTopProcesses = recTop_->value();
    r.frameRate = frameRate_->currentData().toInt();
    r.bloom = bloom_->isChecked();
    r.lowEffects = lowEffects_->isChecked();
    r.decimalUnits = units_->currentData().toInt() == 1;
    r.fahrenheit = temperature_->currentData().toInt() == 1;
    r.checkUpdates = checkUpdates_->isChecked();
    r.updateManifestUrl = manifestUrl_->text().trimmed().toStdString();
    // Providers that are not in the table (absent on this machine) keep whatever was saved for them.
    for (int i = 0; i < providers_->rowCount(); ++i) {
        const auto* item = providers_->item(i, 0);
        if (item->checkState() == Qt::Unchecked) {
            if (!r.disabledProviders.contains(item->text())) r.disabledProviders << item->text();
        } else {
            r.disabledProviders.removeAll(item->text());
        }
    }
    return r;
}

}  // namespace pmp::gui
