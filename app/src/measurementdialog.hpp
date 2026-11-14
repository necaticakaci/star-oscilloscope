// SPDX-License-Identifier: GPL-3.0-or-later
// Chooses the measurements of the table and the reference levels of the timing measurements.
#pragma once

#include <array>
#include <map>

#include <QDialog>

#include "measurementselection.hpp"

class QCheckBox;
class QGroupBox;
class QSpinBox;

class MeasurementDialog : public QDialog {
    Q_OBJECT

public:
    MeasurementDialog(const MeasurementSelection& selection, const starosc::MeasureThresholds& thresholds,
                      QWidget* parent = nullptr);

    MeasurementSelection selection() const;
    starosc::MeasureThresholds thresholds() const;

    // Checkbox of a parameter on a trace, or between the channels with `trace` -1.
    QCheckBox* checkBox(starosc::Parameter parameter, int trace) const;
    QSpinBox* lowThreshold() const { return low_; }
    QSpinBox* midThreshold() const { return mid_; }
    QSpinBox* highThreshold() const { return high_; }

private:
    using TraceBoxes = std::array<QCheckBox*, MeasurementSelection::kTraces>;

    QGroupBox* createTraceGroup(const QString& title, std::size_t first, std::size_t last);
    QGroupBox* createPairGroup();
    QGroupBox* createThresholdGroup();
    void showChoice(const MeasurementSelection& selection, const starosc::MeasureThresholds& thresholds);
    void keepThresholdsInOrder();

    std::map<starosc::Parameter, TraceBoxes> traceBoxes_;
    std::map<starosc::Parameter, QCheckBox*> pairBoxes_;
    QSpinBox* low_ = nullptr;
    QSpinBox* mid_ = nullptr;
    QSpinBox* high_ = nullptr;
};
