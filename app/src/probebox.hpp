// SPDX-License-Identifier: GPL-3.0-or-later
// The probe factor of a channel: ×1, ×10, ×100 or a measured value typed in, such as 10.15.
#pragma once

#include <QComboBox>

#include "starosc/settings.hpp"

class ProbeBox : public QComboBox {
    Q_OBJECT

public:
    explicit ProbeBox(QWidget* parent = nullptr);

    static bool isPreset(double probe);

    // Lists the presets and the factor typed last, and shows the factor in use. Never emits probeChosen.
    void showProbe(const starosc::ChannelSettings& channel);

signals:
    void probeChosen(double probe);

private:
    void readText();

    starosc::ChannelSettings shown_;
    bool updating_ = false;
};
