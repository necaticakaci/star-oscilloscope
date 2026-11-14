// SPDX-License-Identifier: GPL-3.0-or-later
// What the oscilloscope's control panels share. The user picks one in the View menu, the Form or the Front Panel; both
// are kept up to date, so they always show the same settings.
#pragma once

#include <QWidget>

#include "starosc/settings.hpp"

class ScopePanel : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;

    // Shows `settings` without emitting settingsChanged.
    void setSettings(const starosc::ScopeSettings& settings);
    const starosc::ScopeSettings& settings() const { return settings_; }

    // Rate of the record now on the screen, which limits the zoom timebases on offer; 0 when unknown.
    void setSampleRate(double hz);

    // Read-only line about the capture, e.g. "1 MSa/s · 5000 pts".
    virtual void setAcquisitionInfo(const QString& text) = 0;
    // What reference `ref` holds: empty `description` when nothing.
    virtual void setReferenceState(int ref, const QString& description, bool shown) = 0;
    // After a trace colour changed.
    virtual void refreshColors() = 0;

signals:
    void settingsChanged(const starosc::ScopeSettings& settings);
    // `source`: 0 or 1 for a channel, 2 for the math trace (starosc::kMathSource).
    void referenceStoreRequested(int ref, int source);
    void referenceShown(int ref, bool shown);
    void referenceSaveRequested(int ref);
    void referenceLoadRequested(int ref);
    void referenceClearRequested(int ref);

protected:
    virtual void showSettings() = 0;

    // Keeps about the same input sensitivity: the V/div goes to the nearest round step of the new factor, and the
    // trigger stays at the same input voltage.
    void changeProbe(int channel, double probe);

    starosc::ScopeSettings settings_;
    double sampleRate_ = 0;
};
