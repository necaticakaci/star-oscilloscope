// SPDX-License-Identifier: GPL-3.0-or-later
// One row of generator controls under the scope (concept G3); the Generator tab holds the rest.
#pragma once

#include <QWidget>

#include "starosc/generator.hpp"

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class SiSpinBox;

class GeneratorStrip : public QWidget {
    Q_OBJECT

public:
    explicit GeneratorStrip(QWidget* parent = nullptr);

    // Shows `settings` without emitting settingsChanged.
    void setSettings(const starosc::GeneratorSettings& settings);
    const starosc::GeneratorSettings& settings() const { return settings_; }
    // Whether the model sets amplitude and offset (GeneratorPanel::setLevelControl).
    void setLevelControl(bool on);

signals:
    void settingsChanged(const starosc::GeneratorSettings& settings);
    void moreRequested();

private:
    void showSettings();
    void readWidgets();

    starosc::GeneratorSettings settings_;
    bool updating_ = false;
    QPushButton* output_ = nullptr;
    QComboBox* waveform_ = nullptr;
    SiSpinBox* frequency_ = nullptr;
    SiSpinBox* amplitude_ = nullptr;
    SiSpinBox* offset_ = nullptr;
    QSpinBox* duty_ = nullptr;
    QLabel* sweep_ = nullptr;
};
