// SPDX-License-Identifier: GPL-3.0-or-later
#include "measurementselection.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <QChar>
#include <QCoreApplication>

#include "starosc/scale.hpp"

namespace {

// Digits line up when the unit takes the same room: "1.000 kHz" over "500.0 µs ".
constexpr int kUnitWidth = 3;

QString withUnitField(const QString& text)
{
    const qsizetype unit = text.size() - text.lastIndexOf(QLatin1Char(' ')) - 1;
    return text + QString(std::max<qsizetype>(0, kUnitWidth - unit), QChar(QChar::Nbsp));
}

}  // namespace

QString formatMeasurement(starosc::Parameter p, double value, std::string_view voltage_unit)
{
    if (!std::isfinite(value))
        return QStringLiteral("-----") + QString(kUnitWidth + 1, QChar(QChar::Nbsp));
    std::string unit(starosc::parameter_unit(p));
    if (unit == "%" || unit == "°") {
        const int decimals = unit == "%" ? 1 : 2;
        const double scale = std::pow(10.0, decimals);
        const double rounded = std::round(value * scale) / scale;  // no "-0.00"
        return withUnitField(QString::number(rounded == 0 ? 0.0 : rounded, 'f', decimals) + " " + QString::fromStdString(unit));
    }
    if (unit == "V")
        unit = voltage_unit;
    return withUnitField(QString::fromStdString(starosc::format_si(value, unit)));
}

QString parameterName(starosc::Parameter p)
{
    using P = starosc::Parameter;
    switch (p) {
    case P::Frequency: return QCoreApplication::translate("Measurement", "Frequency");
    case P::Period: return QCoreApplication::translate("Measurement", "Period");
    case P::RiseTime: return QCoreApplication::translate("Measurement", "Rise time");
    case P::FallTime: return QCoreApplication::translate("Measurement", "Fall time");
    case P::PositiveWidth: return QCoreApplication::translate("Measurement", "Positive width");
    case P::NegativeWidth: return QCoreApplication::translate("Measurement", "Negative width");
    case P::Duty: return QCoreApplication::translate("Measurement", "Duty cycle");
    case P::PeakToPeak: return QCoreApplication::translate("Measurement", "Peak-to-peak");
    case P::Amplitude: return QCoreApplication::translate("Measurement", "Amplitude");
    case P::Max: return QCoreApplication::translate("Measurement", "Maximum");
    case P::Min: return QCoreApplication::translate("Measurement", "Minimum");
    case P::Top: return QCoreApplication::translate("Measurement", "Top");
    case P::Base: return QCoreApplication::translate("Measurement", "Base");
    case P::Mean: return QCoreApplication::translate("Measurement", "Mean");
    case P::Rms: return QCoreApplication::translate("Measurement", "RMS");
    case P::AcRms: return QCoreApplication::translate("Measurement", "AC RMS");
    case P::Overshoot: return QCoreApplication::translate("Measurement", "Overshoot");
    case P::Undershoot: return QCoreApplication::translate("Measurement", "Undershoot");
    case P::Delay: return QCoreApplication::translate("Measurement", "Delay");
    case P::Phase: return QCoreApplication::translate("Measurement", "Phase");
    }
    return {};
}

QString shortParameterName(starosc::Parameter p)
{
    using P = starosc::Parameter;
    // Short names on the display, beside the value: "Freq 1.000 kHz".
    switch (p) {
    case P::Frequency: return QCoreApplication::translate("Measurement", "Freq", "short name of Frequency");
    case P::Period: return QCoreApplication::translate("Measurement", "Per", "short name of Period");
    case P::RiseTime: return QCoreApplication::translate("Measurement", "Rise", "short name of Rise time");
    case P::FallTime: return QCoreApplication::translate("Measurement", "Fall", "short name of Fall time");
    case P::PositiveWidth: return QCoreApplication::translate("Measurement", "+Wid", "short name of Positive width");
    case P::NegativeWidth: return QCoreApplication::translate("Measurement", "-Wid", "short name of Negative width");
    case P::Duty: return QCoreApplication::translate("Measurement", "Duty", "short name of Duty cycle");
    case P::PeakToPeak: return QCoreApplication::translate("Measurement", "Vpp", "short name of Peak-to-peak");
    case P::Amplitude: return QCoreApplication::translate("Measurement", "Ampl", "short name of Amplitude");
    case P::Max: return QCoreApplication::translate("Measurement", "Max", "short name of Maximum");
    case P::Min: return QCoreApplication::translate("Measurement", "Min", "short name of Minimum");
    case P::Top: return QCoreApplication::translate("Measurement", "Top", "short name of Top");
    case P::Base: return QCoreApplication::translate("Measurement", "Base", "short name of Base");
    case P::Mean: return QCoreApplication::translate("Measurement", "Mean", "short name of Mean");
    case P::Rms: return QCoreApplication::translate("Measurement", "RMS", "short name of RMS");
    case P::AcRms: return QCoreApplication::translate("Measurement", "ACrms", "short name of AC RMS");
    case P::Overshoot: return QCoreApplication::translate("Measurement", "Over", "short name of Overshoot");
    case P::Undershoot: return QCoreApplication::translate("Measurement", "Undr", "short name of Undershoot");
    case P::Delay: return QCoreApplication::translate("Measurement", "Dly", "short name of Delay");
    case P::Phase: return QCoreApplication::translate("Measurement", "Phs", "short name of Phase");
    }
    return {};
}
