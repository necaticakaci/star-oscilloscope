// SPDX-License-Identifier: GPL-3.0-or-later
#include "sispinbox.hpp"

#include <algorithm>
#include <cmath>

#include <QRegularExpression>

#include "starosc/scale.hpp"

namespace {

struct Prefix {
    double scale;
    const char* name;
};

constexpr Prefix kPrefixes[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}};

std::vector<double> steps125(double lo, double hi)
{
    std::vector<double> out;
    for (double decade = std::pow(10.0, std::floor(std::log10(lo))); decade <= hi; decade *= 10)
        for (double m : {1.0, 2.0, 5.0})
            if (decade * m >= lo * (1 - 1e-9) && decade * m <= hi * (1 + 1e-9))
                out.push_back(decade * m);
    return out;
}

}  // namespace

SiSpinBox::SiSpinBox(const QString& unit, double resolution, QWidget* parent)
    : QDoubleSpinBox(parent), unit_(unit), resolution_(resolution)
{
    setDecimals(std::max(0, static_cast<int>(std::ceil(-std::log10(resolution)))));
    setKeyboardTracking(false);
    setAccelerated(true);
}

std::optional<double> SiSpinBox::parse(QString text, const QString& unit)
{
    text = text.trimmed().replace(',', '.');
    if (!unit.isEmpty() && text.endsWith(unit, Qt::CaseInsensitive))
        text.chop(unit.size());
    static const QRegularExpression number(R"(^([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*([GMkKmuµ]?)\s*$)");
    const QRegularExpressionMatch m = number.match(text.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    double value = m.captured(1).toDouble(&ok);
    if (!ok)
        return std::nullopt;
    const QString prefix = m.captured(2);
    if (prefix == "G")
        value *= 1e9;
    else if (prefix == "M")
        value *= 1e6;
    else if (prefix == "k" || prefix == "K")
        value *= 1e3;
    else if (prefix == "m")
        value *= 1e-3;
    else if (prefix == "u" || prefix == "µ")
        value *= 1e-6;
    return value;
}

QString SiSpinBox::textFromValue(double value) const
{
    const double magnitude = std::abs(value);
    Prefix chosen{1, ""};
    if (magnitude > 0)
        for (const Prefix& p : kPrefixes)
            if (magnitude >= p.scale * (1 - 1e-9) || p.scale <= resolution_) {
                chosen = p;
                break;
            }
    const int digits = std::clamp(static_cast<int>(std::lround(std::log10(chosen.scale / resolution_))), 0, 9);
    return QString("%1 %2%3").arg(QString::number(value / chosen.scale, 'f', digits), chosen.name, unit_);
}

double SiSpinBox::valueFromText(const QString& text) const { return parse(text, unit_).value_or(value()); }

QValidator::State SiSpinBox::validate(QString& text, int&) const
{
    const auto v = parse(text, unit_);
    if (!v)
        return QValidator::Intermediate;
    return *v >= minimum() && *v <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
}

void SiSpinBox::fixup(QString& text) const
{
    if (const auto v = parse(text, unit_))
        text = textFromValue(std::clamp(*v, minimum(), maximum()));
}

void SiSpinBox::stepBy(int steps)
{
    if (!steps125_)
        return QDoubleSpinBox::stepBy(steps);
    const std::vector<double> values = steps125(std::max(minimum(), resolution_), maximum());
    setValue(starosc::step_value(values, value(), steps));
    selectAll();
}
