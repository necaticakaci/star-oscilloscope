// SPDX-License-Identifier: GPL-3.0-or-later
// The Generator tab (concept G1): a preview of the output with its levels, and every generator setting.
#pragma once

#include <array>

#include <QCoreApplication>
#include <QIcon>
#include <QWidget>

#include "starosc/generator.hpp"
#include "starosc/settings.hpp"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class SiSpinBox;

// Pieces shared by the Generator tab and the generator strip under the scope.
namespace generator_ui {

QString waveformName(starosc::Waveform waveform);
QIcon waveformIcon(starosc::Waveform waveform);
// "sine, 1.000 kHz, 3.000 Vpp, 500 mV" or "sweep 100 Hz – 10.00 kHz, …"
QString summary(const starosc::GeneratorSettings& settings);

QPushButton* outputButton(QWidget* parent);
void showOutput(QPushButton* button, bool on);
SiSpinBox* frequencyBox(QWidget* parent);
SiSpinBox* amplitudeBox(QWidget* parent);
SiSpinBox* offsetBox(QWidget* parent);
QSpinBox* dutyBox(QWidget* parent);

}  // namespace generator_ui

class GeneratorPreview : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(GeneratorPreview)

public:
    explicit GeneratorPreview(QWidget* parent = nullptr);
    void setSettings(const starosc::GeneratorSettings& settings);
    QSize sizeHint() const override { return {520, 380}; }
    QSize minimumSizeHint() const override { return {260, 190}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    starosc::GeneratorSettings settings_;
};

class GeneratorPanel : public QWidget {
    Q_OBJECT

public:
    explicit GeneratorPanel(QWidget* parent = nullptr);

    // Shows `settings` without emitting settingsChanged.
    void setSettings(const starosc::GeneratorSettings& settings);
    const starosc::GeneratorSettings& settings() const { return settings_; }
    void setParkOutput(bool on);
    // Whether the model sets amplitude and offset (the ISDS205X); without them they are off, and so is parking.
    void setLevelControl(bool on);
    // Which inputs are on their most sensitive stages, which raise the output (starosc::Frame::sensitive).
    void setSensitiveInputs(const std::array<bool, starosc::kChannels>& sensitive);

signals:
    void settingsChanged(const starosc::GeneratorSettings& settings);
    void parkOutputChanged(bool on);

private:
    QWidget* createPreview();
    QWidget* createControls();
    void showSettings();
    void readWidgets();

    starosc::GeneratorSettings settings_;
    bool updating_ = false;
    std::array<bool, starosc::kChannels> sensitive_{};

    GeneratorPreview* preview_ = nullptr;
    std::array<QLabel*, 4> readouts_{};
    QLabel* outputState_ = nullptr;   // the preview's status strip: output on or parked,
    QLabel* previewScale_ = nullptr;  // its scale,
    QLabel* outputLimits_ = nullptr;  // and the limits its dashed lines show
    QLabel* warnings_ = nullptr;
    QPushButton* output_ = nullptr;
    QCheckBox* park_ = nullptr;
    QLabel* noLevels_ = nullptr;  // why amplitude, offset and parking are off
    QButtonGroup* waveforms_ = nullptr;
    SiSpinBox* frequency_ = nullptr;
    SiSpinBox* amplitude_ = nullptr;
    SiSpinBox* offset_ = nullptr;
    QSpinBox* duty_ = nullptr;
    QGroupBox* sweep_ = nullptr;
    SiSpinBox* sweepStart_ = nullptr;
    SiSpinBox* sweepStop_ = nullptr;
    QDoubleSpinBox* sweepTime_ = nullptr;
    QComboBox* sweepSpacing_ = nullptr;
};
