// SPDX-License-Identifier: GPL-3.0-or-later
// The oscilloscope's controls as an instrument's front panel: knobs, keys and value boxes. It holds every setting of
// the Form; what the tool bar offers (Run, Single, Force, Autoset) stays on the tool bar.
#pragma once

#include <array>

#include "scopepanel.hpp"

class Knob;
class ProbeBox;
class QButtonGroup;
class QComboBox;
class QGroupBox;
class QLabel;
class QToolButton;

class FrontPanel : public ScopePanel {
    Q_OBJECT

public:
    explicit FrontPanel(QWidget* parent = nullptr);

    void setAcquisitionInfo(const QString& text) override;
    void setReferenceState(int ref, const QString& description, bool shown) override;
    void refreshColors() override;

    // The light in the trigger section: the frame on the screen was triggered.
    void setTriggered(bool triggered);

protected:
    void showSettings() override;

private:
    struct ChannelControls {
        QToolButton* key = nullptr;
        Knob* position = nullptr;
        Knob* scale = nullptr;
        QButtonGroup* coupling = nullptr;
        ProbeBox* probe = nullptr;
    };

    struct ReferenceControls {
        QToolButton* key = nullptr;
        QComboBox* source = nullptr;
        QLabel* description = nullptr;
    };

    QGroupBox* createVertical();
    QWidget* createChannel(int channel, QWidget* parent);
    QGroupBox* createMath();
    QGroupBox* createReferences();
    QGroupBox* createHorizontal();
    QGroupBox* createTrigger();

    // Applies a change of the user's; nothing happens when it changes nothing.
    template <typename Change>
    void edit(Change change);

    std::array<ChannelControls, starosc::kChannels> channels_;

    QToolButton* mathKey_ = nullptr;
    QComboBox* mathA_ = nullptr;
    QComboBox* mathOperation_ = nullptr;
    QComboBox* mathB_ = nullptr;
    Knob* mathPosition_ = nullptr;
    Knob* mathScale_ = nullptr;

    std::array<ReferenceControls, 2> references_{};

    Knob* timeScale_ = nullptr;
    Knob* zoom_ = nullptr;
    Knob* zoomPosition_ = nullptr;
    QButtonGroup* memory_ = nullptr;
    QButtonGroup* display_ = nullptr;
    QLabel* acquisitionInfo_ = nullptr;

    Knob* triggerLevel_ = nullptr;
    Knob* sensitivity_ = nullptr;
    Knob* preTrigger_ = nullptr;
    QWidget* triggeredLight_ = nullptr;
    QButtonGroup* triggerSource_ = nullptr;
    QButtonGroup* triggerEdge_ = nullptr;
    QButtonGroup* triggerMode_ = nullptr;
};
