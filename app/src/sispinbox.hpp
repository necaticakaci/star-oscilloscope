// SPDX-License-Identifier: GPL-3.0-or-later
// Spin box that shows and reads values with SI prefixes: "1.000 kHz", "300 mV". Typing "1,5k" or "2 MHz" works.
#pragma once

#include <optional>

#include <QDoubleSpinBox>

class SiSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    // `resolution` is the smallest step the value takes, e.g. 1 Hz; it sets the digits shown.
    SiSpinBox(const QString& unit, double resolution, QWidget* parent = nullptr);

    // With 1-2-5 steps the arrows and the wheel move along 1, 2, 5, 10, …; otherwise by singleStep().
    void setStepsOf125(bool on) { steps125_ = on; }

    // The value of `text` in base units, or nothing. A bare number is taken in base units.
    static std::optional<double> parse(QString text, const QString& unit);

    void stepBy(int steps) override;

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& text, int& pos) const override;
    void fixup(QString& text) const override;

private:
    QString unit_;
    double resolution_;
    bool steps125_ = false;
};
