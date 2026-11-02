// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/gui/app_settings.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>

#include <vector>

namespace pmp::gui {

// Settings (§8.5): General, Sampling, Display, Privacy, Advanced and About tabs. The dialog edits a copy;
// the caller applies result() when the dialog is accepted.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const AppSettings& settings, std::vector<ProviderHealth> health, QWidget* parent = nullptr);

    AppSettings result() const;
    QTabWidget* tabs() const { return tabs_; }
    QTableWidget* providerTable() const { return providers_; }
    QLineEdit* manifestUrlEdit() const { return manifestUrl_; }
    QPushButton* checkNowButton() const { return checkNow_; }
    QComboBox* tombstoneCombo() const { return tombstone_; }
    QCheckBox* processHistoryBox() const { return processHistory_; }
    QCheckBox* environmentBox() const { return environment_; }
    QCheckBox* resolveHostsBox() const { return resolveHosts_; }
    QComboBox* videoFpsCombo() const { return videoFps_; }
    QCheckBox* recordingRingBox() const { return recRing_; }
    QSpinBox* recordingRingMinutesSpin() const { return recRingMinutes_; }
    QSpinBox* recordingMaxMinutesSpin() const { return recMaxMinutes_; }
    QSpinBox* recordingTopSpin() const { return recTop_; }
    void setUpdateStatus(const QString& text) { updateStatus_->setText(text); }
    QString updateStatusText() const { return updateStatus_->text(); }

signals:
    void checkNowRequested(const QString& manifestUrl);

private:
    QWidget* generalTab();
    QWidget* samplingTab();
    QWidget* displayTab();
    QWidget* privacyTab();
    QWidget* advancedTab(const std::vector<ProviderHealth>& health);
    QWidget* aboutTab();

    AppSettings s_;
    QTabWidget* tabs_;
    QCheckBox* launchAtLogin_ = nullptr;
    QComboBox* interval_ = nullptr;
    QComboBox* history_ = nullptr;
    QSpinBox* topCount_ = nullptr;
    QComboBox* tombstone_ = nullptr;
    QCheckBox* processHistory_ = nullptr;
    QCheckBox* environment_ = nullptr;
    QCheckBox* resolveHosts_ = nullptr;
    QComboBox* videoFps_ = nullptr;
    QCheckBox* recRing_ = nullptr;
    QSpinBox *recRingMinutes_ = nullptr, *recMaxMinutes_ = nullptr, *recTop_ = nullptr;
    QComboBox* frameRate_ = nullptr;
    QCheckBox* bloom_ = nullptr;
    QCheckBox* lowEffects_ = nullptr;
    QComboBox* units_ = nullptr;
    QComboBox* temperature_ = nullptr;
    QCheckBox* checkUpdates_ = nullptr;
    QLineEdit* manifestUrl_ = nullptr;
    QPushButton* checkNow_ = nullptr;
    QLabel* updateStatus_ = nullptr;
    QTableWidget* providers_ = nullptr;
};

}  // namespace pmp::gui
