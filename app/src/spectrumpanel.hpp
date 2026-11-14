// SPDX-License-Identifier: GPL-3.0-or-later
// Docked form of the Spectrum tab: traces and their inputs, frequency, level, averaging and analysis.
// The input scale and coupling are the oscilloscope's channel settings, shared with its controls.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <QWidget>

#include "starosc/settings.hpp"
#include "starosc/spectrum.hpp"

// The name of an FFT window in the language of the interface.
QString windowName(starosc::Window window);

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class SiSpinBox;

class SpectrumPanel : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumPanel(QWidget* parent = nullptr);

    // Show the settings without emitting their signals.
    void setSettings(const starosc::SpectrumSettings& settings);
    // After a trace colour changed.
    void refreshColors();
    void setScopeSettings(const starosc::ScopeSettings& settings);
    const starosc::SpectrumSettings& settings() const { return settings_; }
    // Rates of the open source, ascending.
    void setSampleRates(const std::vector<std::uint32_t>& rates);
    // Read-only line under the frequency group, e.g. "bin 15.26 Hz · RBW 22.9 Hz".
    void setResolutionInfo(const QString& text);

signals:
    void settingsChanged(const starosc::SpectrumSettings& settings);
    void scopeSettingsChanged(const starosc::ScopeSettings& settings);
    void restartRequested();

private:
    struct TraceWidgets {
        QCheckBox* shown = nullptr;
        QComboBox* scale = nullptr;     // channels only
        QComboBox* coupling = nullptr;  // channels only
        QLabel* range = nullptr;        // input range, or the math function
    };

    QWidget* createTraces();
    QWidget* createFrequency();
    QWidget* createLevel();
    QWidget* createAveraging();
    QWidget* createAnalysis();
    void showSettings();
    void readWidgets();
    void readScope();

    starosc::SpectrumSettings settings_;
    starosc::ScopeSettings scope_;
    std::vector<std::uint32_t> rates_;
    bool updating_ = false;

    std::array<TraceWidgets, starosc::kSpectrumSources> traces_;
    QComboBox* rate_ = nullptr;
    QComboBox* points_ = nullptr;
    QComboBox* window_ = nullptr;
    QComboBox* axis_ = nullptr;
    SiSpinBox* start_ = nullptr;
    SiSpinBox* stop_ = nullptr;
    QLabel* resolution_ = nullptr;
    QComboBox* unit_ = nullptr;
    QDoubleSpinBox* reference_ = nullptr;
    QComboBox* dbPerDiv_ = nullptr;
    QComboBox* averaging_ = nullptr;
    QSpinBox* averages_ = nullptr;
    QSpinBox* harmonics_ = nullptr;
    QComboBox* markers_ = nullptr;
};
