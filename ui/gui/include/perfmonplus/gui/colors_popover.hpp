// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/gui/app_settings.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QSlider>
#include <QWidget>

namespace pmp::gui {

// Appearance controls (§8.6): theme, saturation, bloom, frame rate, low-effects mode.
// Every change is reported immediately; the owner applies it live.
class ColorsPopover : public QWidget {
    Q_OBJECT
public:
    explicit ColorsPopover(const AppSettings& initial, QWidget* parent = nullptr);

    void setSettings(const AppSettings& s);  // refresh the controls without emitting
    QRadioButton* themeButton(const QString& name) const;
    QSlider* saturationSlider() const { return saturation_; }
    QCheckBox* bloomCheck() const { return bloom_; }
    QComboBox* frameRateCombo() const { return frameRate_; }
    QCheckBox* lowEffectsCheck() const { return lowEffects_; }

signals:
    void settingsChanged(pmp::gui::AppSettings settings);

private:
    void emitChanged();

    AppSettings base_;
    QRadioButton* dark_;
    QRadioButton* light_;
    QRadioButton* green_;
    QSlider* saturation_;
    QCheckBox* bloom_;
    QComboBox* frameRate_;
    QCheckBox* lowEffects_;
    bool updating_ = false;
};

}  // namespace pmp::gui
