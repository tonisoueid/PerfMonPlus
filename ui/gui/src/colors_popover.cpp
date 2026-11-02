// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/colors_popover.hpp>

#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace pmp::gui {

ColorsPopover::ColorsPopover(const AppSettings& initial, QWidget* parent)
    : QWidget(parent, Qt::Popup),
      base_(initial),
      dark_(new QRadioButton("Dark", this)),
      light_(new QRadioButton("Light", this)),
      green_(new QRadioButton("Green phosphor", this)),
      saturation_(new QSlider(Qt::Horizontal, this)),
      bloom_(new QCheckBox("Bloom (glow)", this)),
      frameRate_(new QComboBox(this)),
      lowEffects_(new QCheckBox("Low-effects mode", this)) {
    setObjectName("popover");
    dark_->setProperty("theme", "dark");
    light_->setProperty("theme", "light");
    green_->setProperty("theme", "green");
    saturation_->setRange(0, 11);
    for (int hz : {15, 30, 60, 120}) frameRate_->addItem(QString("%1 Hz").arg(hz), hz);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);
    col->addWidget(new QLabel("Appearance", this));
    col->addWidget(dark_);
    col->addWidget(light_);
    col->addWidget(green_);
    col->addWidget(new QLabel("Saturation", this));
    col->addWidget(saturation_);
    col->addWidget(bloom_);
    col->addWidget(new QLabel("Frame rate", this));
    col->addWidget(frameRate_);
    col->addWidget(lowEffects_);
    setSettings(initial);

    for (auto* b : {dark_, light_, green_}) connect(b, &QRadioButton::clicked, this, [this] { emitChanged(); });
    connect(saturation_, &QSlider::valueChanged, this, [this] { emitChanged(); });
    connect(bloom_, &QCheckBox::toggled, this, [this] { emitChanged(); });
    connect(frameRate_, &QComboBox::currentIndexChanged, this, [this] { emitChanged(); });
    connect(lowEffects_, &QCheckBox::toggled, this, [this] { emitChanged(); });
}

QRadioButton* ColorsPopover::themeButton(const QString& name) const {
    for (auto* b : {dark_, light_, green_}) {
        if (b->property("theme").toString() == name) return b;
    }
    return nullptr;
}

void ColorsPopover::setSettings(const AppSettings& s) {
    updating_ = true;
    base_ = s;
    (s.themeName == "light" ? light_ : s.themeName == "green" ? green_ : dark_)->setChecked(true);
    saturation_->setValue(s.saturation);
    bloom_->setChecked(s.bloom && !s.lowEffects);
    bloom_->setEnabled(!s.lowEffects);
    const int idx = frameRate_->findData(s.frameRate);
    frameRate_->setCurrentIndex(idx >= 0 ? idx : frameRate_->findData(60));
    lowEffects_->setChecked(s.lowEffects);
    updating_ = false;
}

void ColorsPopover::emitChanged() {
    if (updating_) return;
    AppSettings s = base_;
    s.themeName = light_->isChecked() ? "light" : green_->isChecked() ? "green" : "dark";
    s.saturation = saturation_->value();
    s.bloom = bloom_->isChecked();
    s.frameRate = frameRate_->currentData().toInt();
    s.lowEffects = lowEffects_->isChecked();
    if (s.lowEffects) {  // low-effects forces the cheap path
        s.bloom = false;
        s.frameRate = std::min(s.frameRate, 30);
    }
    base_ = s;
    updating_ = true;
    bloom_->setEnabled(!s.lowEffects);
    if (s.lowEffects) bloom_->setChecked(false);
    const int idx = frameRate_->findData(s.frameRate);
    if (idx >= 0) frameRate_->setCurrentIndex(idx);
    updating_ = false;
    emit settingsChanged(s);
}

}  // namespace pmp::gui
