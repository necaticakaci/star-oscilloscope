// SPDX-License-Identifier: GPL-3.0-or-later
// Docked form with the channel, timebase, trigger, math and reference settings.
#pragma once

#include <array>

#include "scopepanel.hpp"

class ProbeBox;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QRadioButton;
class QSpinBox;

class ControlPanel : public ScopePanel {
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    void setAcquisitionInfo(const QString& text) override;
    void setReferenceState(int ref, const QString& description, bool shown) override;
    void refreshColors() override;

protected:
    void showSettings() override;

private:
    struct ChannelWidgets {
        QGroupBox* box = nullptr;
        QComboBox* scale = nullptr;
        QDoubleSpinBox* position = nullptr;
        QRadioButton* dc = nullptr;
        QRadioButton* ac = nullptr;
        ProbeBox* probe = nullptr;
    };

    struct ReferenceWidgets {
        QCheckBox* shown = nullptr;
        QComboBox* source = nullptr;
        QLabel* description = nullptr;
    };

    QGroupBox* createChannel(int channel);
    QGroupBox* createTimebase();
    QGroupBox* createTrigger();
    QGroupBox* createMath();
    QGroupBox* createReferences();

    void readWidgets();

    std::array<ChannelWidgets, starosc::kChannels> channels_;
    QComboBox* timeScale_ = nullptr;
    QComboBox* memory_ = nullptr;
    QLabel* acquisitionInfo_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QDoubleSpinBox* zoomPosition_ = nullptr;
    QComboBox* triggerMode_ = nullptr;
    QComboBox* triggerSource_ = nullptr;
    QComboBox* triggerEdge_ = nullptr;
    QDoubleSpinBox* triggerLevel_ = nullptr;
    QDoubleSpinBox* sensitivity_ = nullptr;
    QSpinBox* preTrigger_ = nullptr;
    QComboBox* display_ = nullptr;
    QGroupBox* math_ = nullptr;
    QComboBox* mathA_ = nullptr;
    QComboBox* mathOperation_ = nullptr;
    QComboBox* mathB_ = nullptr;
    QComboBox* mathScale_ = nullptr;
    QDoubleSpinBox* mathPosition_ = nullptr;
    std::array<ReferenceWidgets, 2> references_{};

    bool updating_ = false;
};
